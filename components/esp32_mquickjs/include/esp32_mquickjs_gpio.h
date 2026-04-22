#pragma once

#include "esp32_mquickjs_types.h"

JSValue js_gpio_pinMode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_digitalWrite(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_digitalRead(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_led(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_get_led_builtin(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_get_user_led_pin(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_get_user_led_active_low(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
