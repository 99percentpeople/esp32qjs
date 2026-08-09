#include "esp32_mquickjs_usb_serial.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL

#include "esp32_mquickjs_core.h"
#include "utils/esp32_mquickjs_line_framer.h"

#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_select.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"

#define USB_SERIAL_READ_CHUNK_BYTES 256U

typedef struct {
    bool initialized;
    bool opened;
    bool owns_driver;
    bool callback_added;
    bool polling;
    bool release_buffer;
    esp32_mquickjs_runtime_t *runtime;
    JSGCRef callback;
    char *line_buffer;
    size_t max_frame_bytes;
    esp32_mquickjs_line_framer_t framer;
    uint32_t received_frames;
    uint32_t sent_frames;
    uint32_t overflow_frames;
    uint32_t callback_errors;
} esp32_mquickjs_usb_serial_state_t;

typedef struct {
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
    bool handled;
} esp32_mquickjs_usb_serial_emit_context_t;

static esp32_mquickjs_usb_serial_state_t s_usb_serial_state;

static bool usb_serial_poller(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              void *opaque);

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

static void usb_serial_close_internal(JSContext *ctx)
{
    if (s_usb_serial_state.callback_added) {
        JS_DeleteGCRef(ctx, &s_usb_serial_state.callback);
        s_usb_serial_state.callback_added = false;
    }
    s_usb_serial_state.opened = false;
    if (s_usb_serial_state.polling) {
        s_usb_serial_state.release_buffer = true;
    } else {
        usb_serial_release_line_buffer();
    }
}

static bool usb_serial_call_callback(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime,
                                     const char *error,
                                     const char *frame,
                                     size_t frame_len)
{
    JSGCRef callback_ref;
    JSValue *callback;
    JSValue argv[2];
    JSValue result;

    if (!s_usb_serial_state.opened || !s_usb_serial_state.callback_added) {
        return false;
    }

    callback = JS_PushGCRef(ctx, &callback_ref);
    *callback = s_usb_serial_state.callback.val;
    argv[0] = error != NULL ? JS_NewString(ctx, error) : JS_UNDEFINED;
    argv[1] = frame != NULL ? JS_NewStringLen(ctx, frame, frame_len) : JS_UNDEFINED;
    result = esp32_mquickjs_call(ctx, runtime, *callback, JS_NULL, 2, argv);
    if (JS_IsException(result)) {
        s_usb_serial_state.callback_errors++;
        esp32_mquickjs_print_exception(ctx);
    }
    JS_PopGCRef(ctx, &callback_ref);
    return true;
}

static void usb_serial_emit_frame(void *opaque,
                                  const char *frame,
                                  size_t frame_len,
                                  bool overflow)
{
    esp32_mquickjs_usb_serial_emit_context_t *emit = opaque;

    if (emit == NULL || emit->ctx == NULL) {
        return;
    }
    if (overflow) {
        s_usb_serial_state.overflow_frames++;
        emit->handled = usb_serial_call_callback(
            emit->ctx,
            emit->runtime,
            "serial frame exceeds configured maxFrameBytes",
            NULL,
            0) || emit->handled;
        return;
    }

    s_usb_serial_state.received_frames++;
    emit->handled = usb_serial_call_callback(
        emit->ctx, emit->runtime, NULL, frame, frame_len) || emit->handled;
}

static bool usb_serial_poller(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              void *opaque)
{
    esp32_mquickjs_usb_serial_emit_context_t emit = {
        .ctx = ctx,
        .runtime = runtime,
    };
    uint8_t chunk[USB_SERIAL_READ_CHUNK_BYTES];
    int read_len;

    (void)opaque;
    if (!s_usb_serial_state.initialized || !s_usb_serial_state.opened ||
        s_usb_serial_state.line_buffer == NULL) {
        return false;
    }

    s_usb_serial_state.polling = true;
    do {
        read_len = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), 0);
        if (read_len > 0) {
            esp32_mquickjs_line_framer_feed(&s_usb_serial_state.framer,
                                            chunk,
                                            (size_t)read_len,
                                            usb_serial_emit_frame,
                                            &emit);
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
        s_usb_serial_state.owns_driver = true;
    }

    usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_set_select_notif_callback(usb_serial_notify_from_isr);
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!esp32_mquickjs_register_async_poller(runtime, usb_serial_poller, NULL)) {
        usb_serial_jtag_set_select_notif_callback(NULL);
        usb_serial_jtag_vfs_use_nonblocking();
        if (s_usb_serial_state.owns_driver) {
            usb_serial_jtag_driver_uninstall();
        }
        memset(&s_usb_serial_state, 0, sizeof(s_usb_serial_state));
        JS_ThrowInternalError(ctx, "failed to register USB serial poller");
        return false;
    }

    s_usb_serial_state.initialized = true;
    return true;
}

void esp32_mquickjs_deinit_usb_serial_runtime(JSContext *ctx)
{
    if (!s_usb_serial_state.initialized) {
        return;
    }

    usb_serial_close_internal(ctx);
    usb_serial_jtag_set_select_notif_callback(NULL);
    usb_serial_jtag_vfs_use_nonblocking();
    if (s_usb_serial_state.owns_driver && usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_uninstall();
    }
    memset(&s_usb_serial_state, 0, sizeof(s_usb_serial_state));
}

JSValue js_usb_serial_open(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    JSValue options = JS_UNDEFINED;
    JSValue callback;
    JSValue *callback_ref;
    size_t max_frame_bytes;

    (void)this_val;
    if (!s_usb_serial_state.initialized) {
        return JS_ThrowInternalError(ctx, "usbSerial is not initialized");
    }
    if (s_usb_serial_state.opened || s_usb_serial_state.polling) {
        return JS_ThrowInternalError(ctx, "usbSerial is already open");
    }
    if (argc == 1) {
        callback = argv[0];
    } else if (argc == 2) {
        options = argv[0];
        callback = argv[1];
    } else {
        return JS_ThrowTypeError(ctx,
                                 "usbSerial.open(callback) or usbSerial.open(options, callback) expected");
    }
    if (!JS_IsFunction(ctx, callback)) {
        return JS_ThrowTypeError(ctx, "usbSerial.open() expects a callback function");
    }
    if (!usb_serial_parse_max_frame_bytes(ctx, options, &max_frame_bytes)) {
        return JS_ThrowRangeError(
            ctx,
            "usbSerial.open() maxFrameBytes must be between 256 and %d",
            CONFIG_ESP32_MQUICKJS_USB_SERIAL_MAX_FRAME_BYTES);
    }

    s_usb_serial_state.line_buffer =
        heap_caps_malloc(max_frame_bytes, MALLOC_CAP_8BIT);
    if (s_usb_serial_state.line_buffer == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    s_usb_serial_state.max_frame_bytes = max_frame_bytes;
    esp32_mquickjs_line_framer_init(&s_usb_serial_state.framer,
                                    s_usb_serial_state.line_buffer,
                                    max_frame_bytes);
    callback_ref = JS_AddGCRef(ctx, &s_usb_serial_state.callback);
    *callback_ref = callback;
    s_usb_serial_state.callback_added = true;
    s_usb_serial_state.opened = true;
    return JS_NewBool(true);
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
    usb_serial_close_internal(ctx);
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
    if (!s_usb_serial_state.opened || argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "usbSerial.send(text) expects an open transport and a string");
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
        !esp32_mquickjs_set_property_ref(ctx, status, "maxFrameBytes",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.max_frame_bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "receivedFrames",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.received_frames)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "sentFrames",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.sent_frames)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "overflowFrames",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.overflow_frames)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "callbackErrors",
                                         JS_NewInt32(ctx, (int32_t)s_usb_serial_state.callback_errors))) {
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
