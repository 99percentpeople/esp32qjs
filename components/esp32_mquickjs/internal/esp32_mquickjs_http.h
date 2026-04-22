#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP

bool esp32_mquickjs_init_http_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime);

JSValue js_http_fetch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
