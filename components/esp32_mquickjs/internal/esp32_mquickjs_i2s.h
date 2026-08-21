#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2S

bool esp32_mquickjs_init_i2s_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_i2s_runtime(void);

JSValue js_i2s_input_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_i2s_input_finalizer(JSContext *ctx, void *opaque);
JSValue js_i2s_input_start(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2s_input_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2s_input_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2s_input_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2s_input_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2s_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2s_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
