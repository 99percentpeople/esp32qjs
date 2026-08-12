#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_GPIO

void esp32_mquickjs_deinit_gpio_runtime(JSContext *ctx);

JSValue js_gpio_pinMode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_isValid(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_isOutputCapable(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_setPull(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_configure(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_digitalWrite(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_digitalRead(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_toggle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_getDriveStrength(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_setDriveStrength(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_hold(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_watch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_reset(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_led(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_get_led_builtin(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_get_user_led_pin(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gpio_get_user_led_active_low(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
