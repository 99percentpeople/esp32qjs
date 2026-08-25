#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_NET

#include "esp_err.h"

esp_err_t esp32_mquickjs_net_ensure_initialized(void);
bool esp32_mquickjs_net_is_ready(void);

bool esp32_mquickjs_init_net_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_net_runtime(esp32_mquickjs_runtime_t *runtime);

JSValue js_net_status(JSContext *ctx, JSValue *this_val, int argc,
                      JSValue *argv);
JSValue js_net_watch(JSContext *ctx, JSValue *this_val, int argc,
                     JSValue *argv);

#endif
