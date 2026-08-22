#include "esp32_mquickjs_usb_serial.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "utils/esp32_mquickjs_line_framer.h"

#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_select.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"

#define USB_SERIAL_READ_CHUNK_BYTES 256U
#define USB_SERIAL_JTAG_PACKET_BYTES 64U
#define USB_SERIAL_BINARY_WRITE_CHUNK_BYTES 256U
#define USB_SERIAL_BINARY_WRITE_TIMEOUT_MS 1000U
#define USB_SERIAL_EVENT_QUEUE_LEN \
    ((CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES + \
      USB_SERIAL_JTAG_PACKET_BYTES - 1U) / USB_SERIAL_JTAG_PACKET_BYTES)

typedef struct {
    uint8_t *data;
    size_t length;
    bool overflow;
} esp32_mquickjs_usb_serial_event_t;

typedef struct {
    bool initialized;
    bool opened;
    bool polling;
    bool release_buffer;
    bool binary;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_event_queue_t *event_queue;
    char *line_buffer;
    size_t max_frame_bytes;
    esp32_mquickjs_line_framer_t framer;
    uint32_t received_frames;
    uint32_t sent_frames;
    uint32_t overflow_frames;
} esp32_mquickjs_usb_serial_state_t;

typedef struct {
    bool handled;
} esp32_mquickjs_usb_serial_emit_context_t;

static esp32_mquickjs_usb_serial_state_t s_usb_serial_state;

/*
 * The USB Serial/JTAG VFS console and interrupt-driven driver are physical
 * boot resources. A JavaScript runtime restart must close only the JS-owned
 * stream and event queue; uninstalling and reinstalling the driver here can
 * make the host re-enumerate the USB device and, on ESP32-S3, turn a
 * restartRuntime() into a USB_UART_CHIP_RESET. Keep the driver installed for
 * the lifetime of the firmware boot.
 */
static bool s_usb_serial_driver_ready;

static bool usb_serial_poller(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              void *opaque);

static bool usb_serial_write_binary(const uint8_t *data, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        size_t chunk = length - offset;
        int written;

        if (chunk > USB_SERIAL_BINARY_WRITE_CHUNK_BYTES) {
            chunk = USB_SERIAL_BINARY_WRITE_CHUNK_BYTES;
        }
        written = usb_serial_jtag_write_bytes(
            data + offset,
            chunk,
            pdMS_TO_TICKS(USB_SERIAL_BINARY_WRITE_TIMEOUT_MS));
        if (written != (int)chunk) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

static void usb_serial_notify_from_isr(usj_select_notif_t notification,
                                       int *task_woken)
{
    if (notification == USJ_SELECT_READ_NOTIF &&
        s_usb_serial_state.initialized && s_usb_serial_state.opened) {
        esp32_mquickjs_notify_active_runtime_from_isr(task_woken);
    }
}

static void usb_serial_release_line_buffer(void)
{
    heap_caps_free(s_usb_serial_state.line_buffer);
    s_usb_serial_state.line_buffer = NULL;
    s_usb_serial_state.release_buffer = false;
    esp32_mquickjs_line_framer_init(&s_usb_serial_state.framer, NULL, 0);
}

static void usb_serial_close_source(void *opaque)
{
    (void)opaque;
    s_usb_serial_state.event_queue = NULL;
    s_usb_serial_state.opened = false;
    if (s_usb_serial_state.polling) {
        s_usb_serial_state.release_buffer = true;
    } else {
        usb_serial_release_line_buffer();
    }
}

static void usb_serial_close_internal(void)
{
    esp32_mquickjs_event_queue_t *event_queue = s_usb_serial_state.event_queue;

    if (event_queue != NULL) {
        (void)esp32_mquickjs_event_queue_close(event_queue);
    } else {
        usb_serial_close_source(NULL);
    }
}

static void usb_serial_drop_event(void *data, void *opaque)
{
    esp32_mquickjs_usb_serial_event_t *event = data;

    (void)opaque;
    if (event != NULL) {
        heap_caps_free(event->data);
        event->data = NULL;
    }
}

static JSValue usb_serial_event_to_js(JSContext *ctx, const void *data, void *opaque)
{
    esp32_mquickjs_usb_serial_event_t *event = (esp32_mquickjs_usb_serial_event_t *)data;
    JSValue result;

    (void)opaque;
    if (event->overflow) {
        return JS_ThrowRangeError(ctx, "serial frame exceeds configured maxFrameBytes");
    }
    if (s_usb_serial_state.binary) {
        result = esp32_mquickjs_new_owned_byte_view(ctx, event->data, event->length);
        event->data = NULL;
    } else {
        result = JS_NewStringLen(ctx,
                                 event->data != NULL ? (const char *)event->data : "",
                                 event->length);
        heap_caps_free(event->data);
        event->data = NULL;
    }
    return result;
}

static void usb_serial_emit_frame(void *opaque,
                                  const char *frame,
                                  size_t frame_len,
                                  bool overflow)
{
    esp32_mquickjs_usb_serial_emit_context_t *emit = opaque;
    esp32_mquickjs_usb_serial_event_t event = {
        .length = frame_len,
        .overflow = overflow,
    };

    if (emit == NULL || s_usb_serial_state.event_queue == NULL) {
        return;
    }
    if (overflow) {
        s_usb_serial_state.overflow_frames++;
        emit->handled = esp32_mquickjs_event_queue_send(s_usb_serial_state.event_queue,
                                                       &event) || emit->handled;
        return;
    }

    if (frame_len > 0) {
        event.data = heap_caps_malloc(frame_len, MALLOC_CAP_8BIT);
        if (event.data == NULL) {
            s_usb_serial_state.overflow_frames++;
            return;
        }
        memcpy(event.data, frame, frame_len);
    }
    s_usb_serial_state.received_frames++;
    if (!esp32_mquickjs_event_queue_send(s_usb_serial_state.event_queue, &event)) {
        heap_caps_free(event.data);
    } else {
        emit->handled = true;
    }
}

static bool usb_serial_poller(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              void *opaque)
{
    esp32_mquickjs_usb_serial_emit_context_t emit = {0};
    uint8_t chunk[USB_SERIAL_READ_CHUNK_BYTES];
    int read_len;

    (void)opaque;
    (void)ctx;
    (void)runtime;
    if (!s_usb_serial_state.initialized || !s_usb_serial_state.opened ||
        (!s_usb_serial_state.binary && s_usb_serial_state.line_buffer == NULL)) {
        return false;
    }

    s_usb_serial_state.polling = true;
    do {
        read_len = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), 0);
        if (read_len > 0) {
            if (s_usb_serial_state.binary) {
                usb_serial_emit_frame(&emit,
                                      (const char *)chunk,
                                      (size_t)read_len,
                                      false);
            } else {
                esp32_mquickjs_line_framer_feed(&s_usb_serial_state.framer,
                                                chunk,
                                                (size_t)read_len,
                                                usb_serial_emit_frame,
                                                &emit);
            }
        }
    } while (read_len > 0 && s_usb_serial_state.opened);
    s_usb_serial_state.polling = false;

    if (s_usb_serial_state.release_buffer) {
        usb_serial_release_line_buffer();
    }
    return emit.handled;
}

static bool usb_serial_parse_max_frame_bytes(JSContext *ctx,
                                              JSValue options,
                                              size_t *out_max_frame_bytes)
{
    JSValue value;
    int raw_value;

    *out_max_frame_bytes = CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES;
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0) {
        return false;
    }

    value = JS_GetPropertyStr(ctx, options, "maxFrameBytes");
    if (JS_IsUndefined(value)) {
        return true;
    }
    if (JS_ToInt32(ctx, &raw_value, value) != 0 || raw_value < 256 ||
        raw_value > CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES) {
        return false;
    }
    *out_max_frame_bytes = (size_t)raw_value;
    return true;
}

static bool usb_serial_parse_binary_mode(JSContext *ctx,
                                         JSValue options,
                                         bool *out_binary)
{
    JSGCRef value_ref;
    JSValue *value;
    JSCStringBuf mode_buf;
    const char *mode;

    *out_binary = false;
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    value = JS_PushGCRef(ctx, &value_ref);
    *value = JS_GetPropertyStr(ctx, options, "mode");
    if (JS_IsException(*value)) {
        JS_PopGCRef(ctx, &value_ref);
        return false;
    }
    if (JS_IsUndefined(*value)) {
        JS_PopGCRef(ctx, &value_ref);
        return true;
    }
    if (!JS_IsString(ctx, *value)) {
        JS_PopGCRef(ctx, &value_ref);
        return false;
    }
    mode = JS_ToCString(ctx, *value, &mode_buf);
    if (mode == NULL || (strcmp(mode, "text") != 0 && strcmp(mode, "binary") != 0)) {
        JS_PopGCRef(ctx, &value_ref);
        return false;
    }
    *out_binary = strcmp(mode, "binary") == 0;
    JS_PopGCRef(ctx, &value_ref);
    return true;
}

bool esp32_mquickjs_init_usb_serial_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime)
{
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err;

    if (s_usb_serial_state.initialized) {
        s_usb_serial_state.runtime = runtime;
        return true;
    }

    memset(&s_usb_serial_state, 0, sizeof(s_usb_serial_state));
    s_usb_serial_state.runtime = runtime;
    if (!s_usb_serial_driver_ready || !usb_serial_jtag_is_driver_installed()) {
        if (!usb_serial_jtag_is_driver_installed()) {
            config.rx_buffer_size = CONFIG_ESP32_MQUICKJS_USB_SERIAL_RX_BUFFER_SIZE;
            config.tx_buffer_size = CONFIG_ESP32_MQUICKJS_USB_SERIAL_TX_BUFFER_SIZE;
            err = usb_serial_jtag_driver_install(&config);
            if (err != ESP_OK) {
                JS_ThrowInternalError(ctx,
                                      "failed to install USB serial driver: %s",
                                      esp_err_to_name(err));
                return false;
            }
        }
        usb_serial_jtag_vfs_use_driver();
        setvbuf(stdin, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IONBF, 0);
        s_usb_serial_driver_ready = true;
    } else {
        /* Reattach the retained physical console to the new generation. */
        usb_serial_jtag_vfs_use_driver();
    }

    usb_serial_jtag_set_select_notif_callback(usb_serial_notify_from_isr);
    if (!esp32_mquickjs_register_async_poller(runtime, usb_serial_poller, NULL)) {
        usb_serial_jtag_set_select_notif_callback(NULL);
        memset(&s_usb_serial_state, 0, sizeof(s_usb_serial_state));
        JS_ThrowInternalError(ctx, "failed to register USB serial poller");
        return false;
    }

    s_usb_serial_state.initialized = true;
    return true;
}

void esp32_mquickjs_deinit_usb_serial_runtime(JSContext *ctx)
{
    (void)ctx;
    if (!s_usb_serial_state.initialized) {
        return;
    }

    /* Close generation-owned JS state only. The physical driver, VFS mode,
     * and USB connection stay up across restartRuntime(). */
    usb_serial_close_internal();
    usb_serial_jtag_set_select_notif_callback(NULL);
    memset(&s_usb_serial_state, 0, sizeof(s_usb_serial_state));
}

JSValue js_usb_serial_open(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    JSValue options = JS_UNDEFINED;
    JSGCRef queue_ref;
    JSGCRef receive_ref;
    JSGCRef send_ref;
    JSGCRef status_ref;
    JSValue *queue_object;
    JSValue *receive;
    JSValue *send;
    JSValue *status;
    size_t max_frame_bytes;
    bool binary;

    (void)this_val;
    if (!s_usb_serial_state.initialized) {
        return JS_ThrowInternalError(ctx, "usbSerial is not initialized");
    }
    if (s_usb_serial_state.opened || s_usb_serial_state.polling) {
        return JS_ThrowInternalError(ctx, "usbSerial is already open");
    }
    if (argc > 1) {
        return JS_ThrowTypeError(ctx, "usbSerial.open(options?) expects at most one options object");
    }
    if (argc == 1) {
        options = argv[0];
    }
    if (!usb_serial_parse_max_frame_bytes(ctx, options, &max_frame_bytes)) {
        return JS_ThrowRangeError(
            ctx,
            "usbSerial.open() maxFrameBytes must be between 256 and %d",
            CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES);
    }
    if (!usb_serial_parse_binary_mode(ctx, options, &binary)) {
        return JS_ThrowTypeError(ctx,
                                 "usbSerial.open() mode must be \"text\" or \"binary\"");
    }

    if (!binary) {
        s_usb_serial_state.line_buffer =
            heap_caps_malloc(max_frame_bytes, MALLOC_CAP_8BIT);
        if (s_usb_serial_state.line_buffer == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
    }
    s_usb_serial_state.binary = binary;
    s_usb_serial_state.max_frame_bytes = max_frame_bytes;
    esp32_mquickjs_line_framer_init(&s_usb_serial_state.framer,
                                    s_usb_serial_state.line_buffer,
                                    max_frame_bytes);
    queue_object = JS_PushGCRef(ctx, &queue_ref);
    receive = JS_PushGCRef(ctx, &receive_ref);
    send = JS_PushGCRef(ctx, &send_ref);
    status = JS_PushGCRef(ctx, &status_ref);
    *queue_object = esp32_mquickjs_event_queue_new(
        ctx,
        s_usb_serial_state.runtime,
        sizeof(esp32_mquickjs_usb_serial_event_t),
        USB_SERIAL_EVENT_QUEUE_LEN,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        usb_serial_event_to_js,
        usb_serial_drop_event,
        usb_serial_close_source,
        NULL);
    *receive = JS_IsException(*queue_object)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *queue_object, "receive");
    *send = JS_GetPropertyStr(ctx, *this_val, "send");
    *status = JS_GetPropertyStr(ctx, *this_val, "status");
    if (JS_IsException(*queue_object) || JS_IsException(*receive) ||
        JS_IsException(*send) || JS_IsException(*status) ||
        JS_IsException(JS_SetPropertyStr(ctx, *queue_object, "recv", *receive)) ||
        JS_IsException(JS_SetPropertyStr(ctx, *queue_object, "send", *send)) ||
        JS_IsException(JS_SetPropertyStr(ctx, *queue_object, "status", *status))) {
        usb_serial_release_line_buffer();
        JS_PopGCRef(ctx, &status_ref);
        JS_PopGCRef(ctx, &send_ref);
        JS_PopGCRef(ctx, &receive_ref);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_EXCEPTION;
    }
    s_usb_serial_state.event_queue = JS_GetOpaque(ctx, *queue_object);
    s_usb_serial_state.opened = true;
    JS_PopGCRef(ctx, &status_ref);
    JS_PopGCRef(ctx, &send_ref);
    JS_PopGCRef(ctx, &receive_ref);
    return JS_PopGCRef(ctx, &queue_ref);
}

JSValue js_usb_serial_close(JSContext *ctx,
                            JSValue *this_val,
                            int argc,
                            JSValue *argv)
{
    bool was_open = s_usb_serial_state.opened;

    (void)this_val;
    (void)argc;
    (void)argv;
    usb_serial_close_internal();
    return JS_NewBool(was_open);
}

JSValue js_usb_serial_send(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    JSCStringBuf text_buf;
    const char *text;
    size_t text_len = 0;
    size_t written;

    (void)this_val;
    if (!s_usb_serial_state.opened || argc < 1) {
        return JS_ThrowTypeError(ctx, "usbSerial.send(data) expects an open transport and data");
    }
    if (s_usb_serial_state.binary) {
        int class_id = JS_GetClassID(ctx, argv[0]);

        if (class_id == JS_CLASS_BYTE_SPAN_SOURCE ||
            class_id == JS_CLASS_BITMAP_SPAN_SOURCE) {
            esp32_mquickjs_byte_span_source_t source;
            JSValue error = JS_UNDEFINED;
            size_t total = 0;

            if (!esp32_mquickjs_open_byte_span_source(ctx, argv[0],
                                                      "usbSerial.send(data)",
                                                      &source, &error)) {
                return JS_IsUndefined(error) ? JS_EXCEPTION : error;
            }
            flockfile(stdout);
            while (true) {
                esp32_mquickjs_byte_span_t span;
                if (!esp32_mquickjs_byte_span_source_next(ctx, &source, &span)) {
                    if (JS_HasException(ctx)) {
                        esp32_mquickjs_byte_span_source_close(ctx, &source);
                        funlockfile(stdout);
                        return JS_EXCEPTION;
                    }
                    break;
                }
                if (span.length == 0) {
                    continue;
                }
                if (!usb_serial_write_binary(span.data, span.length)) {
                    esp32_mquickjs_byte_span_source_close(ctx, &source);
                    funlockfile(stdout);
                    return JS_ThrowInternalError(ctx, "usbSerial.send() failed");
                }
                total += span.length;
            }
            esp32_mquickjs_byte_span_source_close(ctx, &source);
            funlockfile(stdout);
            s_usb_serial_state.sent_frames++;
            return JS_NewInt64(ctx, (int64_t)total);
        } else {
            esp32_mquickjs_byte_source_t source;
            uint8_t *owned = NULL;
            JSValue error = JS_UNDEFINED;

            if (!esp32_mquickjs_get_byte_source(ctx, argv[0],
                                                "usbSerial.send(data)",
                                                &source, &owned, &error)) {
                return JS_IsUndefined(error) ? JS_EXCEPTION : error;
            }
            flockfile(stdout);
            written = usb_serial_write_binary(source.data, source.length)
                ? source.length
                : 0;
            esp32_mquickjs_release_byte_source(owned);
            if (written != source.length) {
                funlockfile(stdout);
                return JS_ThrowInternalError(ctx, "usbSerial.send() failed");
            }
            funlockfile(stdout);
            s_usb_serial_state.sent_frames++;
            return JS_NewInt64(ctx, (int64_t)written);
        }
    }
    if (!JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "usbSerial.send(text) expects a string in text mode");
    }
    text = JS_ToCStringLen(ctx, &text_len, argv[0], &text_buf);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    if (text_len > s_usb_serial_state.max_frame_bytes) {
        return JS_ThrowRangeError(ctx, "usbSerial.send() frame is too large");
    }
    if (memchr(text, '\n', text_len) != NULL || memchr(text, '\r', text_len) != NULL) {
        return JS_ThrowTypeError(ctx, "usbSerial.send() text must contain exactly one line");
    }

    flockfile(stdout);
    written = fwrite(text, 1, text_len, stdout);
    if (written == text_len && fputc('\n', stdout) != EOF) {
        fflush(stdout);
        s_usb_serial_state.sent_frames++;
    } else {
        funlockfile(stdout);
        return JS_ThrowInternalError(ctx, "usbSerial.send() failed");
    }
    funlockfile(stdout);
    return JS_NewInt64(ctx, (int64_t)text_len);
}

JSValue js_usb_serial_status(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    JSGCRef status_ref;
    JSValue *status;

    (void)this_val;
    (void)argc;
    (void)argv;
    status = JS_PushGCRef(ctx, &status_ref);
    *status = JS_NewObject(ctx);
    if (JS_IsException(*status) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "open",
                                         JS_NewBool(s_usb_serial_state.opened)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "connected",
                                         JS_NewBool(usb_serial_jtag_is_connected())) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "mode",
                                         JS_NewString(ctx,
                                             s_usb_serial_state.binary ? "binary" : "text")) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "maxFrameBytes",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.max_frame_bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "receivedFrames",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.received_frames)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "sentFrames",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.sent_frames)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "overflowFrames",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.overflow_frames)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "droppedFrames",
                                         JS_NewUint32(ctx,
                                             esp32_mquickjs_event_queue_dropped(
                                                 s_usb_serial_state.event_queue)))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &status_ref);
}

JSValue js_usb_serial_get_max_frame_bytes(JSContext *ctx,
                                           JSValue *this_val,
                                           int argc,
                                           JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES);
}

#endif
