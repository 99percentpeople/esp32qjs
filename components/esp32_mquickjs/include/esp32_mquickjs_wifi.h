#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_init_wifi_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime);

JSValue js_wifi_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_disconnect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

esp_err_t esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *status);
