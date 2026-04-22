#pragma once

#include "esp32_mquickjs_types.h"

void esp32_mquickjs_init_i2c_runtime(void);

JSValue js_i2c_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_writeRead(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_sda(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_scl(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_freq_hz(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_i2c_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
