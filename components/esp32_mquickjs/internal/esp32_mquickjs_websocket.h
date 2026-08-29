#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET

bool esp32_mquickjs_init_websocket_runtime(JSContext *ctx,
                                            esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_deinit_websocket_runtime(JSContext *ctx);

JSValue js_websocket_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_websocket_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_websocket_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_websocket_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_websocket_get_max_message_bytes(JSContext *ctx,
                                            JSValue *this_val,
                                            int argc,
                                            JSValue *argv);

#endif
