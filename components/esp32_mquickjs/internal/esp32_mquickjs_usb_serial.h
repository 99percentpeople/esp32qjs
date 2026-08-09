#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL

bool esp32_mquickjs_init_usb_serial_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_usb_serial_runtime(JSContext *ctx);

JSValue js_usb_serial_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_usb_serial_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_usb_serial_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_usb_serial_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_usb_serial_get_max_frame_bytes(JSContext *ctx,
                                           JSValue *this_val,
                                           int argc,
                                           JSValue *argv);

#endif
