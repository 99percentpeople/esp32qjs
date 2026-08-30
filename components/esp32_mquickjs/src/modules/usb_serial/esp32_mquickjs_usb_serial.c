#include "esp32_mquickjs_usb_serial.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_timer_resource.h"
#include "esp32_mquickjs_usb_serial_future_lifecycle.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "utils/esp32_mquickjs_line_framer.h"

#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_select.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#define USB_SERIAL_READ_CHUNK_BYTES 256U
#define USB_SERIAL_JTAG_PACKET_BYTES 64U
#define USB_SERIAL_BINARY_WRITE_CHUNK_BYTES 256U
#define USB_SERIAL_BINARY_WRITE_STALL_TIMEOUT_MS 1000U
#define USB_SERIAL_EVENT_QUEUE_LEN \
    ((CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES + \
      USB_SERIAL_JTAG_PACKET_BYTES - 1U) / USB_SERIAL_JTAG_PACKET_BYTES)

typedef struct {
    uint8_t *data;
    size_t length;
    bool overflow;
} esp32_mquickjs_usb_serial_event_t;

typedef struct {
    uint32_t generation;
} esp32_mquickjs_usb_serial_ref_t;

typedef struct {
    bool initialized;
    bool opened;
    bool polling;
    bool release_buffer;
    bool binary;
    uint32_t generation;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_event_queue_t *event_queue;
    char *line_buffer;
    size_t max_frame_bytes;
    esp32_mquickjs_line_framer_t framer;
    uint32_t received_frames;
    uint32_t sent_frames;
    uint32_t overflow_frames;
    bool sending;
} esp32_mquickjs_usb_serial_state_t;

typedef struct {
    bool handled;
} esp32_mquickjs_usb_serial_emit_context_t;

static esp32_mquickjs_usb_serial_state_t s_usb_serial_state;
static _Atomic uint32_t s_usb_serial_next_generation = 1;
static esp32_mquickjs_usb_serial_ref_t s_usb_serial_closed_ref;

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
static bool usb_serial_register_future_driver(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime);
static void usb_serial_future_timeout_wake(void *opaque);

typedef enum {
    USB_SERIAL_WRITE_OK = 0,
    USB_SERIAL_WRITE_DISCONNECTED,
    USB_SERIAL_WRITE_CLOSED,
    USB_SERIAL_WRITE_TIMEOUT,
    USB_SERIAL_WRITE_INTERRUPTED,
} esp32_mquickjs_usb_serial_write_result_t;

static esp32_mquickjs_usb_serial_write_result_t usb_serial_write_binary(
    JSContext *ctx,
    const uint8_t *data,
    size_t length)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    uint64_t progress_deadline_us =
        (uint64_t)esp_timer_get_time() +
        ((uint64_t)USB_SERIAL_BINARY_WRITE_STALL_TIMEOUT_MS * 1000ULL);
    size_t offset = 0;

    while (offset < length) {
        size_t chunk = length - offset;
        uint64_t now_us;
        uint64_t remaining_us;
        uint32_t wait_ms;
        esp32_mquickjs_poll_result_t poll_result;
        int written;

        if (!s_usb_serial_state.initialized || !s_usb_serial_state.opened) {
            return USB_SERIAL_WRITE_CLOSED;
        }
        if (!usb_serial_jtag_is_connected()) {
            return USB_SERIAL_WRITE_DISCONNECTED;
        }
        if (chunk > USB_SERIAL_BINARY_WRITE_CHUNK_BYTES) {
            chunk = USB_SERIAL_BINARY_WRITE_CHUNK_BYTES;
        }
        written = usb_serial_jtag_write_bytes(
            data + offset,
            chunk,
            0);
        if (written > 0) {
            offset += (size_t)written;
            progress_deadline_us =
                (uint64_t)esp_timer_get_time() +
                ((uint64_t)USB_SERIAL_BINARY_WRITE_STALL_TIMEOUT_MS * 1000ULL);
            continue;
        }

        poll_result = esp32_mquickjs_poll(ctx, runtime);
        if (!esp32_mquickjs_cooperate(runtime)) {
            return USB_SERIAL_WRITE_INTERRUPTED;
        }
        now_us = (uint64_t)esp_timer_get_time();
        if (now_us >= progress_deadline_us) {
            return USB_SERIAL_WRITE_TIMEOUT;
        }
        if (poll_result != ESP32_MQUICKJS_POLL_NONE) {
            continue;
        }
        remaining_us = progress_deadline_us - now_us;
        wait_ms = (uint32_t)((remaining_us + 999ULL) / 1000ULL);
        (void)esp32_mquickjs_wait_for_activity(runtime, wait_ms);
        if (!esp32_mquickjs_cooperate(runtime)) {
            return USB_SERIAL_WRITE_INTERRUPTED;
        }
    }
    return USB_SERIAL_WRITE_OK;
}

static JSValue usb_serial_write_error(
    JSContext *ctx,
    esp32_mquickjs_usb_serial_write_result_t result)
{
    switch (result) {
    case USB_SERIAL_WRITE_DISCONNECTED:
        return JS_ThrowReferenceError(
            ctx, "usbSerial.send() failed because USB Serial/JTAG is not connected");
    case USB_SERIAL_WRITE_CLOSED:
        return JS_ThrowReferenceError(
            ctx, "usbSerial.send() was interrupted because the transport was closed");
    case USB_SERIAL_WRITE_TIMEOUT:
        return JS_ThrowInternalError(
            ctx, "usbSerial.send() timed out waiting for USB transmit capacity");
    case USB_SERIAL_WRITE_INTERRUPTED:
        return JS_ThrowInternalError(
            ctx, "usbSerial.send() was interrupted by a runtime stop request");
    case USB_SERIAL_WRITE_OK:
    default:
        return JS_ThrowInternalError(ctx, "usbSerial.send() failed");
    }
}

static void usb_serial_notify_from_isr(usj_select_notif_t notification,
                                       int *task_woken)
{
    if ((notification == USJ_SELECT_READ_NOTIF ||
         notification == USJ_SELECT_WRITE_NOTIF ||
         notification == USJ_SELECT_ERROR_NOTIF) &&
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
        event.data = esp32_mquickjs_memory_payload_alloc(
            frame_len, ESP32_MQUICKJS_MEMORY_EXTERNAL);
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

static bool usb_serial_parse_frame_limit(JSContext *ctx,
                                         JSValue options,
                                         bool binary,
                                         size_t *out_max_frame_bytes)
{
    JSValue value;
    int raw_value;
    int minimum = binary ? (int)USB_SERIAL_JTAG_PACKET_BYTES : 256;
    const char *name = binary ? "chunkBytes" : "maxFrameBytes";

    *out_max_frame_bytes = CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES;
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0) {
        return false;
    }

    value = JS_GetPropertyStr(ctx, options, name);
    if (JS_IsUndefined(value)) {
        return true;
    }
    if (JS_ToInt32(ctx, &raw_value, value) != 0 || raw_value < minimum ||
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
    if (!usb_serial_register_future_driver(ctx, runtime)) {
        usb_serial_jtag_set_select_notif_callback(NULL);
        memset(&s_usb_serial_state, 0, sizeof(s_usb_serial_state));
        return false;
    }
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

static uint32_t usb_serial_next_generation(void)
{
    uint32_t generation = atomic_fetch_add_explicit(
        &s_usb_serial_next_generation, 1, memory_order_relaxed);

    if (generation == 0) {
        generation = atomic_fetch_add_explicit(
            &s_usb_serial_next_generation, 1, memory_order_relaxed);
    }
    return generation;
}

static esp32_mquickjs_usb_serial_ref_t *usb_serial_ref_from_value(
    JSContext *ctx, JSValue value, bool require_open)
{
    esp32_mquickjs_usb_serial_ref_t *ref;

    if (JS_GetClassID(ctx, value) != JS_CLASS_USB_SERIAL_HANDLE ||
        (ref = JS_GetOpaque(ctx, value)) == NULL ||
        ref == &s_usb_serial_closed_ref) {
        JS_ThrowTypeError(ctx, "expected a USBSerialHandle");
        return NULL;
    }
    if (require_open &&
        (!s_usb_serial_state.opened ||
         ref->generation != s_usb_serial_state.generation)) {
        JS_ThrowReferenceError(ctx, "USB_SERIAL_STALE_HANDLE: handle is closed");
        return NULL;
    }
    return ref;
}

JSValue js_usb_serial_handle_constructor(JSContext *ctx, JSValue *this_val,
                                         int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx,
                             "USBSerialHandle cannot be constructed directly");
}

void js_usb_serial_handle_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_usb_serial_ref_t *ref = opaque;

    (void)ctx;
    if (ref != NULL && ref != &s_usb_serial_closed_ref) {
        if (s_usb_serial_state.opened &&
            ref->generation == s_usb_serial_state.generation) {
            esp32_mquickjs_event_queue_request_close(
                s_usb_serial_state.event_queue);
        }
        heap_caps_free(ref);
    }
}

JSValue js_usb_serial_receive(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    JSGCRef receiver_ref, method_ref;
    JSValue *receiver = JS_PushGCRef(ctx, &receiver_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    if (this_val == NULL ||
        usb_serial_ref_from_value(ctx, *this_val, true) == NULL) {
        JS_PopGCRef(ctx, &method_ref);
        JS_PopGCRef(ctx, &receiver_ref);
        return JS_EXCEPTION;
    }
    *receiver = *this_val;
    *method = JS_GetPropertyStr(ctx, *receiver, "receive");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *receiver, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    return result;
}

JSValue js_usb_serial_stats(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    if (this_val == NULL ||
        usb_serial_ref_from_value(ctx, *this_val, true) == NULL) {
        return JS_EXCEPTION;
    }
    return js_event_queue_stats(ctx, this_val, argc, argv);
}

JSValue js_usb_serial_capabilities(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    JSGCRef result_ref, modes_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *modes = JS_PushGCRef(ctx, &modes_ref);

    (void)this_val;
    (void)argv;
    if (argc != 0) {
        JS_PopGCRef(ctx, &modes_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowTypeError(ctx,
                                 "usbSerial.capabilities() expects no arguments");
    }
    *result = JS_NewObject(ctx);
    *modes = JS_NewArray(ctx, 2);
    if (!JS_IsException(*modes)) {
        (void)JS_SetPropertyUint32(ctx, *modes, 0,
                                   JS_NewString(ctx, "text"));
        (void)JS_SetPropertyUint32(ctx, *modes, 1,
                                   JS_NewString(ctx, "binary"));
    }
    if (JS_IsException(*result) || JS_IsException(*modes) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "apiVersion",
                                         JS_NewString(ctx, "v1")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "target",
                                         JS_NewString(ctx, CONFIG_IDF_TARGET)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "idfVersion",
                                         JS_NewString(ctx, IDF_VER)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "supported", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "maxFrameBytes",
            JS_NewUint32(ctx,
                         CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "modes", *modes)) {
        *result = JS_EXCEPTION;
    }
    JS_PopGCRef(ctx, &modes_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_usb_serial_open(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    JSValue options = JS_UNDEFINED;
    static const char *const allowed[] = {
        "mode", "maxFrameBytes", "chunkBytes",
    };
    JSGCRef queue_ref;
    JSGCRef handle_ref;
    JSValue *queue_object;
    JSValue *handle;
    esp32_mquickjs_usb_serial_ref_t *ref = NULL;
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
    if (!JS_IsUndefined(options) &&
        !esp32_mquickjs_validate_plain_options(
            ctx, options, "usbSerial.open()", allowed,
            sizeof(allowed) / sizeof(allowed[0]))) {
        return JS_EXCEPTION;
    }
    if (!usb_serial_parse_binary_mode(ctx, options, &binary)) {
        return JS_ThrowTypeError(ctx,
                                 "usbSerial.open() mode must be \"text\" or \"binary\"");
    }
    if (!usb_serial_parse_frame_limit(ctx, options, binary,
                                      &max_frame_bytes)) {
        return JS_ThrowRangeError(
            ctx,
            binary
                ? "usbSerial.open() chunkBytes must be between 64 and %d"
                : "usbSerial.open() maxFrameBytes must be between 256 and %d",
            CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES);
    }

    if (!binary) {
        s_usb_serial_state.line_buffer =
            esp32_mquickjs_memory_payload_alloc(
                max_frame_bytes, ESP32_MQUICKJS_MEMORY_EXTERNAL);
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
    handle = JS_PushGCRef(ctx, &handle_ref);
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
    *handle = JS_NewObjectClassUser(ctx, JS_CLASS_USB_SERIAL_HANDLE);
    if (!JS_IsException(*handle)) {
        ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
        if (ref == NULL) {
            JS_ThrowOutOfMemory(ctx);
            *handle = JS_EXCEPTION;
        } else {
            ref->generation = usb_serial_next_generation();
            JS_SetOpaque(ctx, *handle, ref);
        }
    }
    if (JS_IsException(*queue_object) || JS_IsException(*handle) ||
        !esp32_mquickjs_set_property_ref(ctx, handle, "_eventQueue",
                                         *queue_object)) {
        if (ref != NULL) {
            JS_SetOpaque(ctx, *handle, NULL);
            heap_caps_free(ref);
        }
        usb_serial_release_line_buffer();
        JS_PopGCRef(ctx, &handle_ref);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_EXCEPTION;
    }
    s_usb_serial_state.event_queue = JS_GetOpaque(ctx, *queue_object);
    s_usb_serial_state.generation = ref->generation;
    s_usb_serial_state.opened = true;
    JS_PopGCRef(ctx, &queue_ref);
    return JS_PopGCRef(ctx, &handle_ref);
}

JSValue js_usb_serial_close(JSContext *ctx,
                            JSValue *this_val,
                            int argc,
                            JSValue *argv)
{
    esp32_mquickjs_usb_serial_ref_t *ref;
    bool was_open;

    if (this_val == NULL || argc != 0 ||
        JS_GetClassID(ctx, *this_val) != JS_CLASS_USB_SERIAL_HANDLE) {
        return JS_ThrowTypeError(ctx,
                                 "USBSerialHandle.close() expects no arguments");
    }
    (void)argv;
    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL || ref == &s_usb_serial_closed_ref) {
        return JS_FALSE;
    }
    was_open = s_usb_serial_state.opened &&
               ref->generation == s_usb_serial_state.generation;
    if (was_open) usb_serial_close_internal();
    JS_SetOpaque(ctx, *this_val, &s_usb_serial_closed_ref);
    heap_caps_free(ref);
    return JS_NewBool(was_open);
}

JSValue js_usb_serial_send(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    esp32_mquickjs_usb_serial_write_result_t write_result = USB_SERIAL_WRITE_OK;
    JSCStringBuf text_buf;
    const char *text;
    size_t text_len = 0;

    if (this_val == NULL ||
        usb_serial_ref_from_value(ctx, *this_val, true) == NULL || argc != 1) {
        if (JS_HasException(ctx)) return JS_EXCEPTION;
        return JS_ThrowTypeError(ctx, "usbSerial.send(data) expects an open transport and data");
    }
    if (s_usb_serial_state.sending) {
        return JS_ThrowInternalError(ctx, "usbSerial.send() failed because another send is active");
    }
    if (!usb_serial_jtag_is_connected()) {
        return JS_ThrowReferenceError(
            ctx, "usbSerial.send() failed because USB Serial/JTAG is not connected");
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
            s_usb_serial_state.sending = true;
            flockfile(stdout);
            while (true) {
                esp32_mquickjs_byte_span_t span;

                if (!esp32_mquickjs_byte_span_source_next(ctx, &source, &span)) {
                    break;
                }
                if (span.length == 0) {
                    continue;
                }
                if (span.data == NULL) {
                    JS_ThrowInternalError(
                        ctx,
                        "usbSerial.send(data) received a non-empty span with null data");
                    break;
                }
                write_result = usb_serial_write_binary(
                    ctx, span.data, span.length);
                if (write_result != USB_SERIAL_WRITE_OK) {
                    break;
                }
                total += span.length;
            }
            esp32_mquickjs_byte_span_source_close(ctx, &source);
            funlockfile(stdout);
            s_usb_serial_state.sending = false;
            if (JS_HasException(ctx)) {
                return JS_EXCEPTION;
            }
            if (write_result != USB_SERIAL_WRITE_OK) {
                return usb_serial_write_error(ctx, write_result);
            }
            s_usb_serial_state.sent_frames++;
            return JS_NewInt64(ctx, (int64_t)total);
        } else {
            esp32_mquickjs_byte_source_t source;
            uint8_t *owned = NULL;
            JSValue error = JS_UNDEFINED;
            bool leased = false;

            if (class_id == JS_CLASS_BYTE_VIEW) {
                if (!esp32_mquickjs_byte_view_acquire_read(
                        ctx, argv[0], "usbSerial.send(data)",
                        &source.data, &source.length)) {
                    return JS_EXCEPTION;
                }
                source.owner = argv[0];
                leased = true;
            } else if (!esp32_mquickjs_get_byte_source(ctx, argv[0],
                                                       "usbSerial.send(data)",
                                                       &source, &owned, &error)) {
                return JS_IsUndefined(error) ? JS_EXCEPTION : error;
            }
            s_usb_serial_state.sending = true;
            flockfile(stdout);
            write_result = usb_serial_write_binary(ctx, source.data, source.length);
            funlockfile(stdout);
            s_usb_serial_state.sending = false;
            if (leased) {
                esp32_mquickjs_byte_view_release_read(ctx, argv[0]);
            }
            esp32_mquickjs_release_byte_source(owned);
            if (write_result != USB_SERIAL_WRITE_OK) {
                return usb_serial_write_error(ctx, write_result);
            }
            s_usb_serial_state.sent_frames++;
            return JS_NewInt64(ctx, (int64_t)source.length);
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

    {
        uint8_t *frame = esp32_mquickjs_memory_payload_alloc(
            text_len + 1U, ESP32_MQUICKJS_MEMORY_EXTERNAL);

        if (frame == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
        memcpy(frame, text, text_len);
        frame[text_len] = '\n';
        s_usb_serial_state.sending = true;
        flockfile(stdout);
        write_result = usb_serial_write_binary(ctx, frame, text_len + 1U);
        funlockfile(stdout);
        s_usb_serial_state.sending = false;
        heap_caps_free(frame);
    }
    if (write_result != USB_SERIAL_WRITE_OK) {
        return usb_serial_write_error(ctx, write_result);
    }
    s_usb_serial_state.sent_frames++;
    return JS_NewInt64(ctx, (int64_t)text_len);
}

typedef enum {
    USB_SERIAL_FUTURE_FIXED,
    USB_SERIAL_FUTURE_SPANS,
} usb_serial_future_source_kind_t;

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    usb_serial_future_source_kind_t source_kind;
    esp32_mquickjs_byte_span_source_t span_source;
    esp32_mquickjs_byte_span_t span;
    JSGCRef value_ref;
    const uint8_t *data;
    uint8_t *owned;
    size_t length;
    size_t offset;
    size_t logical_length;
    size_t bytes_sent;
    uint32_t generation;
    uint64_t progress_deadline_us;
    uint64_t progress_timer_delay_us;
    esp32_mquickjs_timer_resource_t progress_timer;
    esp32_mquickjs_usb_serial_write_result_t result;
    esp32_mquickjs_usb_serial_future_lifecycle_t lifecycle;
    bool value_retained;
    bool byte_view_leased;
    bool span_source_opened;
    bool source_failed;
};

static int usb_serial_future_progress_timer_create(
    void *opaque, void **out_timer)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    esp_timer_create_args_t timer_args = {
        .callback = usb_serial_future_timeout_wake,
        .arg = state,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "usb_tx_stall",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer = NULL;
    esp_err_t err;

    if (state == NULL || out_timer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = esp_timer_create(&timer_args, &timer);
    *out_timer = timer;
    return err;
}

static int usb_serial_future_progress_timer_start(void *timer, void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state == NULL || state->progress_timer_delay_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_timer_start_once((esp_timer_handle_t)timer,
                                state->progress_timer_delay_us);
}

static void usb_serial_future_progress_timer_stop(void *timer, void *opaque)
{
    (void)opaque;
    (void)esp_timer_stop((esp_timer_handle_t)timer);
}

static void usb_serial_future_progress_timer_delete(void *timer, void *opaque)
{
    (void)opaque;
    (void)esp_timer_delete((esp_timer_handle_t)timer);
}

static esp32_mquickjs_timer_resource_ops_t
usb_serial_future_progress_timer_ops(
    esp32_mquickjs_future_driver_state_t *state)
{
    return (esp32_mquickjs_timer_resource_ops_t){
        .create = usb_serial_future_progress_timer_create,
        .start = usb_serial_future_progress_timer_start,
        .stop = usb_serial_future_progress_timer_stop,
        .delete_timer = usb_serial_future_progress_timer_delete,
        .opaque = state,
    };
}

static void usb_serial_future_release(
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_timer_resource_ops_t timer_ops;

    if (state == NULL) {
        return;
    }
    timer_ops = usb_serial_future_progress_timer_ops(state);
    esp32_mquickjs_timer_resource_deinit(&state->progress_timer,
                                         &timer_ops);
    if (state->span_source_opened) {
        esp32_mquickjs_byte_span_source_close(state->ctx,
                                               &state->span_source);
        state->span_source_opened = false;
    }
    if (state->byte_view_leased && state->value_retained) {
        esp32_mquickjs_byte_view_release_read(state->ctx,
                                              state->value_ref.val);
        state->byte_view_leased = false;
    }
    if (state->value_retained) {
        JS_DeleteGCRef(state->ctx, &state->value_ref);
        state->value_retained = false;
    }
    heap_caps_free(state->owned);
    heap_caps_free(state);
}

static bool usb_serial_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    JSValue error = JS_UNDEFINED;

    if (out_state == NULL || argc != 1 ||
        !s_usb_serial_state.initialized || !s_usb_serial_state.opened ||
        this_ref == NULL ||
        usb_serial_ref_from_value(ctx, this_ref->val, true) == NULL) {
        if (JS_HasException(ctx)) return false;
        JS_ThrowTypeError(
            ctx,
            "usbSerial.send(data) expects an open transport and one data argument");
        return false;
    }
    if (!usb_serial_jtag_is_connected()) {
        JS_ThrowReferenceError(
            ctx,
            "usbSerial.send() failed because USB Serial/JTAG is not connected");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->generation = s_usb_serial_state.generation;
    state->result = USB_SERIAL_WRITE_OK;
    esp32_mquickjs_byte_span_clear(&state->span);

    if (s_usb_serial_state.binary) {
        int class_id = JS_GetClassID(ctx, argv[0].val);

        if (class_id == JS_CLASS_BYTE_SPAN_SOURCE ||
            class_id == JS_CLASS_BITMAP_SPAN_SOURCE) {
            state->source_kind = USB_SERIAL_FUTURE_SPANS;
            if (!esp32_mquickjs_open_byte_span_source(
                    ctx, argv[0].val, "usbSerial.send(data)",
                    &state->span_source, &error)) {
                usb_serial_future_release(state);
                return false;
            }
            state->span_source_opened = true;
        } else if (class_id == JS_CLASS_BYTE_VIEW) {
            JSValue *value = JS_AddGCRef(ctx, &state->value_ref);

            *value = argv[0].val;
            state->value_retained = true;
            if (!esp32_mquickjs_byte_view_acquire_read(
                    ctx, state->value_ref.val, "usbSerial.send(data)",
                    &state->data, &state->length)) {
                usb_serial_future_release(state);
                return false;
            }
            state->byte_view_leased = true;
            state->logical_length = state->length;
        } else {
            esp32_mquickjs_byte_source_t source;

            if (!esp32_mquickjs_get_byte_source(
                    ctx, argv[0].val, "usbSerial.send(data)",
                    &source, &state->owned, &error)) {
                usb_serial_future_release(state);
                return false;
            }
            state->data = source.data;
            state->length = source.length;
            state->logical_length = source.length;
        }
    } else {
        JSCStringBuf text_buf;
        const char *text;
        size_t text_length = 0;

        if (!JS_IsString(ctx, argv[0].val)) {
            usb_serial_future_release(state);
            JS_ThrowTypeError(
                ctx,
                "usbSerial.send(text) expects a string in text mode");
            return false;
        }
        text = JS_ToCStringLen(ctx, &text_length, argv[0].val, &text_buf);
        if (text == NULL) {
            usb_serial_future_release(state);
            return false;
        }
        if (text_length > s_usb_serial_state.max_frame_bytes) {
            usb_serial_future_release(state);
            JS_ThrowRangeError(ctx, "usbSerial.send() frame is too large");
            return false;
        }
        if (memchr(text, '\n', text_length) != NULL ||
            memchr(text, '\r', text_length) != NULL) {
            usb_serial_future_release(state);
            JS_ThrowTypeError(
                ctx,
                "usbSerial.send() text must contain exactly one line");
            return false;
        }
        state->owned = esp32_mquickjs_memory_payload_alloc(
            text_length + 1U, ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (state->owned == NULL) {
            usb_serial_future_release(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        memcpy(state->owned, text, text_length);
        state->owned[text_length] = '\n';
        state->data = state->owned;
        state->length = text_length + 1U;
        state->logical_length = text_length;
    }
    *out_state = state;
    return true;
}

static void usb_serial_future_timeout_wake(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state != NULL && state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static void usb_serial_future_stop_timeout_wake(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL) {
        esp32_mquickjs_timer_resource_ops_t timer_ops =
            usb_serial_future_progress_timer_ops(state);

        esp32_mquickjs_timer_resource_stop(&state->progress_timer,
                                           &timer_ops);
    }
}

static bool usb_serial_future_arm_timeout_wake(
    esp32_mquickjs_future_driver_state_t *state,
    uint64_t now_us)
{
    esp32_mquickjs_timer_resource_ops_t timer_ops;
    uint64_t remaining_us;

    if (state == NULL || state->progress_timer.timer == NULL ||
        now_us >= state->progress_deadline_us) {
        return false;
    }
    remaining_us = state->progress_deadline_us - now_us;
    timer_ops = usb_serial_future_progress_timer_ops(state);
    esp32_mquickjs_timer_resource_stop(&state->progress_timer, &timer_ops);
    state->progress_timer_delay_us = remaining_us;
    return esp32_mquickjs_timer_resource_start(&state->progress_timer,
                                               &timer_ops) == ESP_OK;
}

static bool usb_serial_future_start(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_timer_resource_ops_t timer_ops;
    esp_err_t timer_err;

    if (state == NULL || s_usb_serial_state.sending) {
        JS_ThrowInternalError(
            ctx,
            "usbSerial.send() failed because another send is active");
        return false;
    }
    if (!s_usb_serial_state.opened ||
        state->generation != s_usb_serial_state.generation ||
        !usb_serial_jtag_is_connected()) {
        JS_ThrowReferenceError(
            ctx,
            "usbSerial.send() failed because USB Serial/JTAG is not connected");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->progress_deadline_us =
        (uint64_t)esp_timer_get_time() +
        (uint64_t)USB_SERIAL_BINARY_WRITE_STALL_TIMEOUT_MS * 1000ULL;
    timer_ops = usb_serial_future_progress_timer_ops(state);
    timer_err = esp32_mquickjs_timer_resource_acquire(
        &state->progress_timer, &timer_ops);
    if (timer_err != ESP_OK) {
        JS_ThrowInternalError(
            ctx, "usbSerial.send() failed to create the TX stall timer");
        return false;
    }
    if (!esp32_mquickjs_usb_serial_future_lifecycle_start(
            &state->lifecycle)) {
        JS_ThrowInternalError(
            ctx, "usbSerial.send() failed to acquire its lifecycle");
        return false;
    }
    s_usb_serial_state.sending = true;
    flockfile(stdout);
    esp32_mquickjs_usb_serial_future_lifecycle_lock_stdout(
        &state->lifecycle);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static void usb_serial_future_step(
    esp32_mquickjs_future_driver_state_t *state)
{
    size_t budget = USB_SERIAL_BINARY_WRITE_CHUNK_BYTES * 4U;

    if (state == NULL ||
        esp32_mquickjs_usb_serial_future_lifecycle_ready(
            &state->lifecycle)) {
        return;
    }
    if (!s_usb_serial_state.initialized || !s_usb_serial_state.opened) {
        state->result = USB_SERIAL_WRITE_CLOSED;
        esp32_mquickjs_usb_serial_future_lifecycle_complete(
            &state->lifecycle);
        return;
    }
    if (!usb_serial_jtag_is_connected()) {
        state->result = USB_SERIAL_WRITE_DISCONNECTED;
        esp32_mquickjs_usb_serial_future_lifecycle_complete(
            &state->lifecycle);
        return;
    }

    while (budget > 0) {
        const uint8_t *data;
        size_t remaining;
        size_t chunk;
        int written;

        if (state->source_kind == USB_SERIAL_FUTURE_SPANS &&
            state->offset >= state->span.length) {
            esp32_mquickjs_byte_span_clear(&state->span);
            if (!esp32_mquickjs_byte_span_source_next(
                    state->ctx, &state->span_source, &state->span)) {
                if (JS_HasException(state->ctx)) {
                    state->source_failed = true;
                }
                esp32_mquickjs_usb_serial_future_lifecycle_complete(
                    &state->lifecycle);
                return;
            }
            state->offset = 0;
            if (state->span.length == 0) {
                continue;
            }
            if (state->span.data == NULL) {
                JS_ThrowInternalError(
                    state->ctx,
                    "usbSerial.send(data) received a non-empty span with null data");
                state->source_failed = true;
                esp32_mquickjs_usb_serial_future_lifecycle_complete(
                    &state->lifecycle);
                return;
            }
        }

        if (state->source_kind == USB_SERIAL_FUTURE_SPANS) {
            data = state->span.data;
            remaining = state->span.length - state->offset;
        } else {
            if (state->offset >= state->length) {
                esp32_mquickjs_usb_serial_future_lifecycle_complete(
                    &state->lifecycle);
                return;
            }
            data = state->data;
            remaining = state->length - state->offset;
        }
        chunk = remaining;
        if (chunk > USB_SERIAL_BINARY_WRITE_CHUNK_BYTES) {
            chunk = USB_SERIAL_BINARY_WRITE_CHUNK_BYTES;
        }
        if (chunk > budget) {
            chunk = budget;
        }
        written = usb_serial_jtag_write_bytes(data + state->offset,
                                              chunk, 0);
        if (written < 0) {
            state->result = USB_SERIAL_WRITE_INTERRUPTED;
            esp32_mquickjs_usb_serial_future_lifecycle_complete(
                &state->lifecycle);
            return;
        }
        if (written == 0) {
            uint64_t now_us = (uint64_t)esp_timer_get_time();

            if (now_us >= state->progress_deadline_us) {
                state->result = USB_SERIAL_WRITE_TIMEOUT;
                esp32_mquickjs_usb_serial_future_lifecycle_complete(
                    &state->lifecycle);
            } else if (!usb_serial_future_arm_timeout_wake(state, now_us)) {
                state->result = USB_SERIAL_WRITE_INTERRUPTED;
                esp32_mquickjs_usb_serial_future_lifecycle_complete(
                    &state->lifecycle);
            }
            return;
        }
        usb_serial_future_stop_timeout_wake(state);
        state->offset += (size_t)written;
        state->bytes_sent += (size_t)written;
        if (state->source_kind == USB_SERIAL_FUTURE_SPANS) {
            state->logical_length += (size_t)written;
        }
        state->progress_deadline_us =
            (uint64_t)esp_timer_get_time() +
            (uint64_t)USB_SERIAL_BINARY_WRITE_STALL_TIMEOUT_MS * 1000ULL;
        budget -= (size_t)written;
    }
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
}

static esp32_mquickjs_future_poll_t usb_serial_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    usb_serial_future_step(state);
    return state != NULL &&
                   esp32_mquickjs_usb_serial_future_lifecycle_ready(
                       &state->lifecycle)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue usb_serial_future_finish(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        esp32_mquickjs_usb_serial_future_lifecycle_cancelled(
            &state->lifecycle)) {
        return JS_ThrowInternalError(ctx, "usbSerial.send() was cancelled");
    }
    if (state->source_failed || JS_HasException(ctx)) {
        return JS_EXCEPTION;
    }
    if (state->result != USB_SERIAL_WRITE_OK) {
        return usb_serial_write_error(ctx, state->result);
    }
    s_usb_serial_state.sent_frames++;
    return JS_NewInt64(ctx, (int64_t)state->logical_length);
}

static esp32_mquickjs_cancel_result_t usb_serial_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        !esp32_mquickjs_usb_serial_future_lifecycle_cancel(
            &state->lifecycle)) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return ESP32_MQUICKJS_CANCELLED;
}

static void usb_serial_future_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    uint32_t release =
        esp32_mquickjs_usb_serial_future_lifecycle_take_release(
            state != NULL ? &state->lifecycle : NULL);

    if ((release & ESP32_MQUICKJS_USB_SERIAL_RELEASE_STDOUT) != 0) {
        funlockfile(stdout);
    }
    if ((release & ESP32_MQUICKJS_USB_SERIAL_RELEASE_SEND_LANE) != 0) {
        s_usb_serial_state.sending = false;
    }
    usb_serial_future_release(state);
}

static esp32_mquickjs_resource_key_t usb_serial_future_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL
               ? (esp32_mquickjs_resource_key_t)&s_usb_serial_state
               : NULL;
}

static const esp32_mquickjs_future_driver_t s_usb_serial_send_driver = {
    .capture = usb_serial_future_prepare,
    .start = usb_serial_future_start,
    .poll = usb_serial_future_poll,
    .finish = usb_serial_future_finish,
    .cancel = usb_serial_future_cancel,
    .destroy = usb_serial_future_destroy,
    .resource_key = usb_serial_future_resource_key,
};

static bool usb_serial_register_future_driver(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref;
    JSGCRef handle_ref;
    JSGCRef send_ref;
    JSGCRef receive_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *handle = JS_PushGCRef(ctx, &handle_ref);
    JSValue *send = JS_PushGCRef(ctx, &send_ref);
    JSValue *receive = JS_PushGCRef(ctx, &receive_ref);
    bool registered;

    *global = JS_GetGlobalObject(ctx);
    *handle = JS_IsException(*global)
                  ? JS_EXCEPTION
                  : JS_NewObjectClassUser(ctx, JS_CLASS_USB_SERIAL_HANDLE);
    *send = JS_IsException(*handle)
                ? JS_EXCEPTION
                : JS_GetPropertyStr(ctx, *handle, "send");
    *receive = JS_IsException(*handle)
                   ? JS_EXCEPTION
                   : JS_GetPropertyStr(ctx, *handle, "receive");
    registered = !JS_IsException(*send) &&
                 esp32_mquickjs_future_register_driver(
                     ctx, runtime, *send, &s_usb_serial_send_driver) &&
                 !JS_IsException(*receive) &&
                 esp32_mquickjs_event_queue_register_receive_alias(
                     ctx, runtime, *receive);
    if (!registered && !JS_HasException(ctx)) {
        JS_ThrowInternalError(
            ctx, "failed to register USB Serial Future driver");
    }
    JS_PopGCRef(ctx, &receive_ref);
    JS_PopGCRef(ctx, &send_ref);
    JS_PopGCRef(ctx, &handle_ref);
    JS_PopGCRef(ctx, &global_ref);
    return registered;
}

JSValue js_usb_serial_status(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    JSGCRef status_ref;
    JSValue *status;

    if (this_val == NULL || argc != 0 ||
        usb_serial_ref_from_value(ctx, *this_val, true) == NULL) {
        if (JS_HasException(ctx)) return JS_EXCEPTION;
        return JS_ThrowTypeError(
            ctx, "USBSerialHandle.status() expects no arguments");
    }
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

#endif
