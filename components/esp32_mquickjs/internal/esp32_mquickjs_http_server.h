#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER

bool esp32_mquickjs_init_http_server_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_http_server_runtime(JSContext *ctx);

JSValue js_http_server_create(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_start(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_remove_route(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_clear_routes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_route(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_respond(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
