#include "esp32_mquickjs_gpio.h"
#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"

static bool s_gpio_hold_state[GPIO_NUM_MAX];

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

static int js_value_to_gpio_index(JSContext *ctx, JSValue value, int *out_pin)
{
    int pin = -1;

    if (JS_ToInt32(ctx, &pin, value) != 0) {
        return -1;
    }

    *out_pin = pin;
    return 0;
}

static const char *gpio_mode_to_string(bool input_enabled, bool output_enabled, bool open_drain)
{
    if (input_enabled && output_enabled && open_drain) {
        return "inputOutputOpenDrain";
    }
    if (input_enabled && output_enabled) {
        return "inputOutput";
    }
    if (output_enabled && open_drain) {
        return "outputOpenDrain";
    }
    if (output_enabled) {
        return "output";
    }
    if (input_enabled) {
        return "input";
    }
    return "disabled";
}

static const char *gpio_pull_to_string(bool pullup_enabled, bool pulldown_enabled)
{
    if (pullup_enabled && pulldown_enabled) {
        return "pullupPulldown";
    }
    if (pullup_enabled) {
        return "pullup";
    }
    if (pulldown_enabled) {
        return "pulldown";
    }
    return "floating";
}

static bool gpio_mode_requires_output(gpio_mode_t mode)
{
    return mode == GPIO_MODE_OUTPUT ||
           mode == GPIO_MODE_OUTPUT_OD ||
           mode == GPIO_MODE_INPUT_OUTPUT ||
           mode == GPIO_MODE_INPUT_OUTPUT_OD;
}

static int gpio_mode_from_string(const char *mode, gpio_mode_t *out_mode)
{
    if (strcmp(mode, "disabled") == 0) {
        *out_mode = GPIO_MODE_DISABLE;
        return 0;
    }
    if (strcmp(mode, "input") == 0) {
        *out_mode = GPIO_MODE_INPUT;
        return 0;
    }
    if (strcmp(mode, "output") == 0) {
        *out_mode = GPIO_MODE_OUTPUT;
        return 0;
    }
    if (strcmp(mode, "inputOutput") == 0 || strcmp(mode, "input_output") == 0) {
        *out_mode = GPIO_MODE_INPUT_OUTPUT;
        return 0;
    }
    if (strcmp(mode, "outputOpenDrain") == 0 || strcmp(mode, "output_open_drain") == 0) {
        *out_mode = GPIO_MODE_OUTPUT_OD;
        return 0;
    }
    if (strcmp(mode, "inputOutputOpenDrain") == 0 ||
        strcmp(mode, "input_output_open_drain") == 0) {
        *out_mode = GPIO_MODE_INPUT_OUTPUT_OD;
        return 0;
    }

    return -1;
}

static int gpio_pull_from_string(const char *pull_mode, gpio_pull_mode_t *out_pull_mode)
{
    if (strcmp(pull_mode, "floating") == 0) {
        *out_pull_mode = GPIO_FLOATING;
        return 0;
    }
    if (strcmp(pull_mode, "pullup") == 0 || strcmp(pull_mode, "pullUp") == 0) {
        *out_pull_mode = GPIO_PULLUP_ONLY;
        return 0;
    }
    if (strcmp(pull_mode, "pulldown") == 0 || strcmp(pull_mode, "pullDown") == 0) {
        *out_pull_mode = GPIO_PULLDOWN_ONLY;
        return 0;
    }
    if (strcmp(pull_mode, "pullupPulldown") == 0 ||
        strcmp(pull_mode, "pullUpPullDown") == 0 ||
        strcmp(pull_mode, "pullup_pulldown") == 0) {
        *out_pull_mode = GPIO_PULLUP_PULLDOWN;
        return 0;
    }

    return -1;
}

static int js_value_to_gpio_mode(JSContext *ctx, JSValue value, gpio_mode_t *out_mode)
{
    JSCStringBuf mode_buf;
    const char *mode;

    if (!JS_IsString(ctx, value)) {
        return -1;
    }

    mode = JS_ToCString(ctx, value, &mode_buf);
    if (mode == NULL) {
        return -1;
    }

    return gpio_mode_from_string(mode, out_mode);
}

static int js_value_to_gpio_pull_mode(JSContext *ctx, JSValue value, gpio_pull_mode_t *out_pull_mode)
{
    JSCStringBuf mode_buf;
    const char *mode;

    if (!JS_IsString(ctx, value)) {
        return -1;
    }

    mode = JS_ToCString(ctx, value, &mode_buf);
    if (mode == NULL) {
        return -1;
    }

    return gpio_pull_from_string(mode, out_pull_mode);
}

static int js_value_to_drive_strength(JSContext *ctx, JSValue value, gpio_drive_cap_t *out_strength)
{
    int raw_strength = 0;

    if (JS_ToInt32(ctx, &raw_strength, value) != 0 ||
        raw_strength < GPIO_DRIVE_CAP_0 ||
        raw_strength >= GPIO_DRIVE_CAP_MAX) {
        return -1;
    }

    *out_strength = (gpio_drive_cap_t)raw_strength;
    return 0;
}

static JSValue gpio_throw_error(JSContext *ctx, esp_err_t err, const char *api_name, gpio_num_t pin)
{
    return JS_ThrowInternalError(ctx, "%s(%d) failed: %s", api_name, (int)pin, esp_err_to_name(err));
}

static JSValue gpio_set_mode(JSContext *ctx, gpio_num_t pin, gpio_mode_t mode)
{
    esp_err_t err;

    if (gpio_mode_requires_output(mode) && !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        return JS_ThrowTypeError(ctx, "GPIO %d does not support output mode", (int)pin);
    }

    err = gpio_set_direction(pin, mode);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_set_direction", pin);
    }

    return JS_NewInt32(ctx, (int32_t)pin);
}

static JSValue gpio_set_pull(JSContext *ctx, gpio_num_t pin, gpio_pull_mode_t pull_mode)
{
    esp_err_t err = gpio_set_pull_mode(pin, pull_mode);

    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_set_pull_mode", pin);
    }

    return JS_NewInt32(ctx, (int32_t)pin);
}

static JSValue gpio_set_drive_strength(JSContext *ctx, gpio_num_t pin, gpio_drive_cap_t strength)
{
    esp_err_t err;

    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        return JS_ThrowTypeError(ctx, "GPIO %d does not support drive strength control", (int)pin);
    }

    err = gpio_set_drive_capability(pin, strength);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_set_drive_capability", pin);
    }

    return JS_NewInt32(ctx, (int32_t)strength);
}

static JSValue gpio_set_hold(JSContext *ctx, gpio_num_t pin, bool enabled)
{
    esp_err_t err;

    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        return JS_ThrowTypeError(ctx, "GPIO %d does not support hold", (int)pin);
    }

    err = enabled ? gpio_hold_en(pin) : gpio_hold_dis(pin);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, enabled ? "gpio_hold_en" : "gpio_hold_dis", pin);
    }

    s_gpio_hold_state[pin] = enabled;
    return JS_NewBool(enabled);
}

static JSValue gpio_make_status(JSContext *ctx, gpio_num_t pin)
{
    JSGCRef status_ref;
    JSValue *status_obj;
    gpio_io_config_t io_config = {0};
    esp_err_t err = gpio_get_io_config(pin, &io_config);

    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_get_io_config", pin);
    }

    status_obj = JS_PushGCRef(ctx, &status_ref);
    *status_obj = JS_NewObject(ctx);
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property(ctx, *status_obj, "pin", JS_NewInt32(ctx, (int32_t)pin)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "valid", JS_NewBool(true)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "outputCapable",
                                     JS_NewBool(GPIO_IS_VALID_OUTPUT_GPIO(pin))) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "mode",
                                     JS_NewString(ctx, gpio_mode_to_string(io_config.ie, io_config.oe, io_config.od))) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "pull",
                                     JS_NewString(ctx, gpio_pull_to_string(io_config.pu, io_config.pd))) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "level",
                                     JS_NewBool(gpio_get_level(pin) != 0)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "inputEnabled",
                                     JS_NewBool(io_config.ie)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "outputEnabled",
                                     JS_NewBool(io_config.oe)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "openDrain",
                                     JS_NewBool(io_config.od)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "pullup",
                                     JS_NewBool(io_config.pu)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "pulldown",
                                     JS_NewBool(io_config.pd)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "driveStrength",
                                     JS_NewInt32(ctx, (int32_t)io_config.drv)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "held",
                                     JS_NewBool(s_gpio_hold_state[pin])) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "functionSelect",
                                     JS_NewUint32(ctx, io_config.fun_sel)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "signalOut",
                                     JS_NewUint32(ctx, io_config.sig_out)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "outputControlledByPeripheral",
                                     JS_NewBool(io_config.oe_ctrl_by_periph)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "outputEnableInverted",
                                     JS_NewBool(io_config.oe_inv)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "sleepEnabled",
                                     JS_NewBool(io_config.slp_sel))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &status_ref);
}

static JSValue gpio_write(JSContext *ctx, gpio_num_t pin, bool level)
{
    gpio_io_config_t io_config = {0};
    esp_err_t err = gpio_get_io_config(pin, &io_config);

    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_get_io_config", pin);
    }

    if (!io_config.oe) {
        JSValue mode_result = gpio_set_mode(ctx, pin, GPIO_MODE_OUTPUT);
        if (JS_IsException(mode_result)) {
            return mode_result;
        }
    }

    err = gpio_set_level(pin, level ? 1 : 0);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_set_level", pin);
    }

    return JS_NewBool(level);
}

JSValue js_gpio_isValid(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int pin = -1;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_index(ctx, argv[0], &pin) != 0) {
        return JS_NewBool(false);
    }

    return JS_NewBool(pin >= 0 && pin < GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(pin));
}

JSValue js_gpio_isOutputCapable(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int pin = -1;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_index(ctx, argv[0], &pin) != 0) {
        return JS_NewBool(false);
    }

    return JS_NewBool(pin >= 0 && pin < GPIO_NUM_MAX && GPIO_IS_VALID_OUTPUT_GPIO(pin));
}

JSValue js_gpio_pinMode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_mode_t mode;
    gpio_num_t pin;

    (void)this_val;

    if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 ||
        js_value_to_gpio_mode(ctx, argv[1], &mode) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.pinMode(pin, mode) expects a valid GPIO and mode string");
    }

    return gpio_set_mode(ctx, pin, mode);
}

JSValue js_gpio_setPull(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    gpio_pull_mode_t pull_mode;

    (void)this_val;

    if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 ||
        js_value_to_gpio_pull_mode(ctx, argv[1], &pull_mode) != 0) {
        return JS_ThrowTypeError(ctx,
                                 "gpio.setPull(pin, mode) expects a valid GPIO and pull mode string");
    }

    return gpio_set_pull(ctx, pin, pull_mode);
}

JSValue js_gpio_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.status(pin) expects a valid GPIO");
    }

    return gpio_make_status(ctx, pin);
}

JSValue js_gpio_configure(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    bool level = false;
    bool level_defined = false;
    bool hold_enabled = false;
    bool hold_defined = false;
    bool mode_defined = false;
    gpio_mode_t mode = GPIO_MODE_DISABLE;
    gpio_drive_cap_t drive_strength = GPIO_DRIVE_CAP_0;
    bool drive_strength_defined = false;
    gpio_pull_mode_t pull_mode = GPIO_FLOATING;
    bool pull_defined = false;
    JSValue property = JS_UNDEFINED;
    JSValue result = JS_UNDEFINED;

    (void)this_val;

    if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 || JS_GetClassID(ctx, argv[1]) < 0) {
        return JS_ThrowTypeError(ctx,
                                 "gpio.configure(pin, options) expects a valid GPIO and options object");
    }

    property = JS_GetPropertyStr(ctx, argv[1], "hold");
    if (JS_IsException(property)) {
        return JS_EXCEPTION;
    }
    if (!JS_IsUndefined(property)) {
        if (js_value_to_bool(ctx, property, &hold_enabled) != 0) {
            return JS_ThrowTypeError(ctx, "gpio.configure({ hold }) expects a boolean-like value");
        }
        hold_defined = true;
        if (!hold_enabled) {
            result = gpio_set_hold(ctx, pin, false);
            if (JS_IsException(result)) {
                return result;
            }
        }
    }

    property = JS_GetPropertyStr(ctx, argv[1], "mode");
    if (JS_IsException(property)) {
        return JS_EXCEPTION;
    }
    if (!JS_IsUndefined(property)) {
        if (js_value_to_gpio_mode(ctx, property, &mode) != 0) {
            return JS_ThrowTypeError(ctx, "gpio.configure({ mode }) expects a GPIO mode string");
        }
        mode_defined = true;
    }

    property = JS_GetPropertyStr(ctx, argv[1], "pull");
    if (JS_IsException(property)) {
        return JS_EXCEPTION;
    }
    if (!JS_IsUndefined(property)) {
        if (js_value_to_gpio_pull_mode(ctx, property, &pull_mode) != 0) {
            return JS_ThrowTypeError(ctx, "gpio.configure({ pull }) expects a GPIO pull mode string");
        }
        pull_defined = true;
    }

    property = JS_GetPropertyStr(ctx, argv[1], "driveStrength");
    if (JS_IsException(property)) {
        return JS_EXCEPTION;
    }
    if (!JS_IsUndefined(property)) {
        if (js_value_to_drive_strength(ctx, property, &drive_strength) != 0) {
            return JS_ThrowTypeError(ctx, "gpio.configure({ driveStrength }) expects 0..3");
        }
        drive_strength_defined = true;
    }

    property = JS_GetPropertyStr(ctx, argv[1], "level");
    if (JS_IsException(property)) {
        return JS_EXCEPTION;
    }
    if (!JS_IsUndefined(property)) {
        if (js_value_to_bool(ctx, property, &level) != 0) {
            return JS_ThrowTypeError(ctx, "gpio.configure({ level }) expects a boolean-like value");
        }
        level_defined = true;
    }

    if (mode_defined) {
        result = gpio_set_mode(ctx, pin, mode);
        if (JS_IsException(result)) {
            return result;
        }
    }

    if (pull_defined) {
        result = gpio_set_pull(ctx, pin, pull_mode);
        if (JS_IsException(result)) {
            return result;
        }
    }

    if (drive_strength_defined) {
        result = gpio_set_drive_strength(ctx, pin, drive_strength);
        if (JS_IsException(result)) {
            return result;
        }
    }

    if (level_defined) {
        if (mode_defined && !gpio_mode_requires_output(mode)) {
            return JS_ThrowTypeError(ctx,
                                     "gpio.configure({ level }) requires an output-capable mode");
        }
        result = gpio_write(ctx, pin, level);
        if (JS_IsException(result)) {
            return result;
        }
    }

    if (hold_defined && hold_enabled) {
        result = gpio_set_hold(ctx, pin, true);
        if (JS_IsException(result)) {
            return result;
        }
    }

    return gpio_make_status(ctx, pin);
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

JSValue js_gpio_toggle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.toggle(pin) expects a valid GPIO");
    }

    return gpio_write(ctx, pin, gpio_get_level(pin) == 0);
}

JSValue js_gpio_getDriveStrength(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    gpio_drive_cap_t strength = GPIO_DRIVE_CAP_0;
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.getDriveStrength(pin) expects a valid GPIO");
    }

    err = gpio_get_drive_capability(pin, &strength);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_get_drive_capability", pin);
    }

    return JS_NewInt32(ctx, (int32_t)strength);
}

JSValue js_gpio_setDriveStrength(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    gpio_drive_cap_t strength = GPIO_DRIVE_CAP_0;

    (void)this_val;

    if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 ||
        js_value_to_drive_strength(ctx, argv[1], &strength) != 0) {
        return JS_ThrowTypeError(ctx,
                                 "gpio.setDriveStrength(pin, strength) expects a valid GPIO and 0..3 strength");
    }
    return gpio_set_drive_strength(ctx, pin, strength);
}

JSValue js_gpio_hold(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    bool enabled;

    (void)this_val;

    if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 ||
        js_value_to_bool(ctx, argv[1], &enabled) != 0) {
        return JS_ThrowTypeError(ctx,
                                 "gpio.hold(pin, enabled) expects a valid GPIO and boolean-like value");
    }
    return gpio_set_hold(ctx, pin, enabled);
}

JSValue js_gpio_reset(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.reset(pin) expects a valid GPIO");
    }

    if (s_gpio_hold_state[pin]) {
        err = gpio_hold_dis(pin);
        if (err != ESP_OK) {
            return gpio_throw_error(ctx, err, "gpio_hold_dis", pin);
        }
        s_gpio_hold_state[pin] = false;
    }

    err = gpio_reset_pin(pin);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_reset_pin", pin);
    }

    return JS_NewInt32(ctx, (int32_t)pin);
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
