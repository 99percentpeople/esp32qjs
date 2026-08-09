#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C

void esp32_mquickjs_init_i2c_runtime(void);
void esp32_mquickjs_deinit_i2c_runtime(void);

JSValue js_i2c_bus_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_i2c_bus_finalizer(JSContext *ctx, void *opaque);
JSValue js_i2c_bus_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_write_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_bus_writeRead(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_i2c_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_sda(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_scl(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_freq_hz(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
