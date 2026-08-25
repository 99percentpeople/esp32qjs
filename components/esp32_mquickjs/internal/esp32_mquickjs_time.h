#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_init_time_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_time_runtime(void);

JSValue js_sys_time_status(JSContext *ctx, JSValue *this_val, int argc,
                           JSValue *argv);

#if CONFIG_ESP32_MQUICKJS_FEATURE_NET
JSValue js_sys_time_sync(JSContext *ctx, JSValue *this_val, int argc,
                         JSValue *argv);
#endif
