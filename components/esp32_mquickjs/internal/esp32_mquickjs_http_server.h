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
JSValue js_http_server_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_post(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_put(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_patch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_delete(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_head(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_options(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_server_all(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER && CONFIG_ESP32_MQUICKJS_FEATURE_FS
JSValue js_http_static_file_handler(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_static_file_handler_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_static_file_handler_handle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
#endif

#endif
