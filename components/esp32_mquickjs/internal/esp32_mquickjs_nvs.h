#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_NVS

bool esp32_mquickjs_init_nvs_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime);

JSValue js_nvs_getString(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_nvs_setString(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_nvs_erase(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_nvs_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_nvs_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_nvs_get_max_value_bytes(JSContext *ctx,
                                    JSValue *this_val,
                                    int argc,
                                    JSValue *argv);

#endif
