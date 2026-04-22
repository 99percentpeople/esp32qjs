#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_init_http_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime);

JSValue js_http_fetch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
