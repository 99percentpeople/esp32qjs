#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C

bool esp32_mquickjs_init_i2c_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_i2c_runtime(void);

JSValue js_i2c_bus_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_i2c_bus_finalizer(JSContext *ctx, void *opaque);
JSValue js_i2c_bus_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_open_device(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_i2c_device_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_i2c_device_finalizer(JSContext *ctx, void *opaque);
JSValue js_i2c_device_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_device_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_device_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_device_write_segments(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_device_write_batch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_device_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_device_write_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_i2c_open_bus(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_sda(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_scl(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_freq_hz(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
