#include "esp32_mquickjs_internal.h"

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

bool esp32_mquickjs_install_gpio_module(JSContext *ctx, JSValue global_obj)
{
    JSGCRef module_ref;
    JSValue *module_obj;

    module_obj = JS_PushGCRef(ctx, &module_ref);
    *module_obj = JS_NewObject(ctx);
    if (JS_IsException(*module_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *module_obj, "INPUT",
                                     JS_NewString(ctx, "input")) ||
        !esp32_mquickjs_set_property(ctx, *module_obj, "OUTPUT",
                                     JS_NewString(ctx, "output")) ||
        !esp32_mquickjs_set_property(ctx, *module_obj, "LED_BUILTIN",
                                     JS_NewInt32(ctx, ESP32_MQUICKJS_USER_LED_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *module_obj, "USER_LED_PIN",
                                     JS_NewInt32(ctx, ESP32_MQUICKJS_USER_LED_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *module_obj, "USER_LED_ACTIVE_LOW",
                                     JS_NewBool(ESP32_MQUICKJS_USER_LED_ACTIVE_LOW)) ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "pinMode", "gpio.pinMode") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "digitalWrite", "gpio.digitalWrite") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "digitalRead", "gpio.digitalRead") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "led", "gpio.led")) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, global_obj, "gpio", JS_PopGCRef(ctx, &module_ref))) {
        return false;
    }
    return true;

fail:
    JS_PopGCRef(ctx, &module_ref);
    return false;
}

bool esp32_mquickjs_dispatch_gpio(JSContext *ctx,
                                  const char *operation,
                                  int argc,
                                  JSValue *argv,
                                  JSValue *result)
{
    if (strcmp(operation, "pinMode") == 0) {
        JSCStringBuf mode_buf;
        const char *mode;
        gpio_num_t pin;

        if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 || !JS_IsString(ctx, argv[1])) {
            *result = JS_ThrowTypeError(ctx, "gpio.pinMode(pin, mode) expects a valid GPIO and mode string");
            return true;
        }

        mode = JS_ToCString(ctx, argv[1], &mode_buf);
        *result = gpio_set_mode(ctx, pin, mode);
        return true;
    }

    if (strcmp(operation, "digitalWrite") == 0) {
        gpio_num_t pin;
        bool level;

        if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 ||
            js_value_to_bool(ctx, argv[1], &level) != 0) {
            *result = JS_ThrowTypeError(ctx,
                                        "gpio.digitalWrite(pin, value) expects a valid GPIO and boolean-like value");
            return true;
        }

        *result = gpio_write(ctx, pin, level);
        return true;
    }

    if (strcmp(operation, "digitalRead") == 0) {
        gpio_num_t pin;

        if (argc < 1 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0) {
            *result = JS_ThrowTypeError(ctx, "gpio.digitalRead(pin) expects a valid GPIO");
            return true;
        }

        *result = JS_NewBool(gpio_get_level(pin) != 0);
        return true;
    }

    if (strcmp(operation, "led") == 0) {
        bool led_on;
        bool gpio_level;
        JSValue write_result;

        if (argc < 1 || js_value_to_bool(ctx, argv[0], &led_on) != 0) {
            *result = JS_ThrowTypeError(ctx, "gpio.led(value) expects a boolean-like value");
            return true;
        }

        gpio_level = ESP32_MQUICKJS_USER_LED_ACTIVE_LOW ? !led_on : led_on;
        write_result = gpio_write(ctx, (gpio_num_t)ESP32_MQUICKJS_USER_LED_PIN, gpio_level);
        if (JS_IsException(write_result)) {
            *result = write_result;
            return true;
        }
        *result = JS_NewBool(led_on);
        return true;
    }

    return false;
}
