#include "esp32_mquickjs_gpio.h"
#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"

static uint64_t s_output_gpio_mask;

static int js_value_to_bool(JSContext *ctx, JSValue value, bool *out_value)
{
    int int_value;

    if (JS_IsBool(value)) {
        *out_value = (value == JS_TRUE);
        return 0;
    }

    if (JS_ToInt32(ctx, &int_value, value) == 0) {
        *out_value = (int_value != 0);
        return 0;
    }

    return -1;
}

static int js_value_to_gpio_num(JSContext *ctx, JSValue value, gpio_num_t *out_pin)
{
    int pin;

    if (JS_ToInt32(ctx, &pin, value) != 0) {
        return -1;
    }
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin)) {
        return -1;
    }

    *out_pin = (gpio_num_t)pin;
    return 0;
}

static JSValue gpio_set_mode(JSContext *ctx, gpio_num_t pin, const char *mode)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (strcmp(mode, "output") == 0) {
        io_cfg.mode = GPIO_MODE_OUTPUT;
    } else if (strcmp(mode, "input") == 0) {
        io_cfg.mode = GPIO_MODE_INPUT;
    } else {
        return JS_ThrowTypeError(ctx, "gpio.pinMode(pin, mode) expects 'input' or 'output'");
    }

    if (gpio_config(&io_cfg) != ESP_OK) {
        return JS_ThrowInternalError(ctx, "gpio_config(%d) failed", (int)pin);
    }

    if (io_cfg.mode == GPIO_MODE_OUTPUT) {
        s_output_gpio_mask |= 1ULL << pin;
    } else {
        s_output_gpio_mask &= ~(1ULL << pin);
    }

    return JS_NewInt32(ctx, (int32_t)pin);
}

static JSValue gpio_write(JSContext *ctx, gpio_num_t pin, bool level)
{
    if ((s_output_gpio_mask & (1ULL << pin)) == 0) {
        JSValue mode_result = gpio_set_mode(ctx, pin, "output");
        if (JS_IsException(mode_result)) {
            return mode_result;
        }
    }

    if (gpio_set_level(pin, level ? 1 : 0) != ESP_OK) {
        return JS_ThrowInternalError(ctx, "gpio_set_level(%d) failed", (int)pin);
    }

    return JS_NewBool(level);
}

JSValue js_gpio_pinMode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSCStringBuf mode_buf;
    const char *mode;
    gpio_num_t pin;

    (void)this_val;

    if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 || !JS_IsString(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "gpio.pinMode(pin, mode) expects a valid GPIO and mode string");
    }

    mode = JS_ToCString(ctx, argv[1], &mode_buf);
    return gpio_set_mode(ctx, pin, mode);
}

JSValue js_gpio_digitalWrite(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    bool level;

    (void)this_val;

    if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 ||
        js_value_to_bool(ctx, argv[1], &level) != 0) {
        return JS_ThrowTypeError(ctx,
                                 "gpio.digitalWrite(pin, value) expects a valid GPIO and boolean-like value");
    }

    return gpio_write(ctx, pin, level);
}

JSValue js_gpio_digitalRead(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.digitalRead(pin) expects a valid GPIO");
    }

    return JS_NewBool(gpio_get_level(pin) != 0);
}

JSValue js_gpio_led(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    bool led_on;
    bool gpio_level;
    JSValue write_result;

    (void)this_val;

    if (argc < 1 || js_value_to_bool(ctx, argv[0], &led_on) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.led(value) expects a boolean-like value");
    }

    gpio_level = ESP32_MQUICKJS_USER_LED_ACTIVE_LOW ? !led_on : led_on;
    write_result = gpio_write(ctx, (gpio_num_t)ESP32_MQUICKJS_USER_LED_PIN, gpio_level);
    if (JS_IsException(write_result)) {
        return write_result;
    }
    return JS_NewBool(led_on);
}

JSValue js_gpio_get_led_builtin(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_USER_LED_PIN);
}

JSValue js_gpio_get_user_led_pin(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_USER_LED_PIN);
}

JSValue js_gpio_get_user_led_active_low(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewBool(ESP32_MQUICKJS_USER_LED_ACTIVE_LOW);
}
