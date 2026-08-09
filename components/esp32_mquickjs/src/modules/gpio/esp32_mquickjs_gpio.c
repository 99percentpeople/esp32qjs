#include "esp32_mquickjs_gpio.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_GPIO

#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define GPIO_INTERRUPT_QUEUE_LEN 16

typedef struct {
    gpio_num_t pin;
    uint32_t generation;
} gpio_interrupt_event_t;

typedef struct {
    JSGCRef callback;
    volatile uint32_t generation;
    volatile uint32_t dropped;
    gpio_int_type_t intr_type;
    bool callback_registered;
    bool handler_installed;
    volatile bool attached;
} gpio_interrupt_slot_t;

static const char *TAG = "esp32_mquickjs_gpio";

static bool s_gpio_hold_state[GPIO_NUM_MAX];
static QueueHandle_t s_gpio_interrupt_queue;
static gpio_interrupt_slot_t s_gpio_interrupt_slots[GPIO_NUM_MAX];
static bool s_gpio_isr_service_installed;
static portMUX_TYPE s_gpio_interrupt_lock = portMUX_INITIALIZER_UNLOCKED;

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

static const char *gpio_interrupt_mode_to_string(gpio_int_type_t intr_type)
{
    switch (intr_type) {
    case GPIO_INTR_POSEDGE:
        return "rising";
    case GPIO_INTR_NEGEDGE:
        return "falling";
    case GPIO_INTR_ANYEDGE:
        return "change";
    case GPIO_INTR_LOW_LEVEL:
        return "low";
    case GPIO_INTR_HIGH_LEVEL:
        return "high";
    default:
        return "disabled";
    }
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

static int gpio_interrupt_mode_from_string(const char *mode, gpio_int_type_t *out_intr_type)
{
    if (strcmp(mode, "change") == 0) {
        *out_intr_type = GPIO_INTR_ANYEDGE;
        return 0;
    }
    if (strcmp(mode, "rising") == 0) {
        *out_intr_type = GPIO_INTR_POSEDGE;
        return 0;
    }
    if (strcmp(mode, "falling") == 0) {
        *out_intr_type = GPIO_INTR_NEGEDGE;
        return 0;
    }
    if (strcmp(mode, "low") == 0) {
        *out_intr_type = GPIO_INTR_LOW_LEVEL;
        return 0;
    }
    if (strcmp(mode, "high") == 0) {
        *out_intr_type = GPIO_INTR_HIGH_LEVEL;
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

static int js_value_to_gpio_interrupt_mode(JSContext *ctx,
                                           JSValue value,
                                           gpio_int_type_t *out_intr_type)
{
    int int_value = -1;
    JSCStringBuf mode_buf;
    const char *mode;

    if (JS_IsString(ctx, value)) {
        mode = JS_ToCString(ctx, value, &mode_buf);
        if (mode == NULL) {
            return -1;
        }

        return gpio_interrupt_mode_from_string(mode, out_intr_type);
    }

    if (JS_IsBool(value) || JS_ToInt32(ctx, &int_value, value) != 0) {
        return -1;
    }

    if (int_value == 0) {
        *out_intr_type = GPIO_INTR_LOW_LEVEL;
        return 0;
    }
    if (int_value == 1) {
        *out_intr_type = GPIO_INTR_HIGH_LEVEL;
        return 0;
    }

    return -1;
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

static JSValue gpio_call_function(JSContext *ctx,
                                  JSValue func,
                                  JSValue this_val,
                                  int argc,
                                  JSValue *argv)
{
    return esp32_mquickjs_call(ctx,
                               esp32_mquickjs_get_active_runtime(),
                               func,
                               this_val,
                               argc,
                               argv);
}

static esp_err_t gpio_interrupt_ensure_queue(void)
{
    if (s_gpio_interrupt_queue != NULL) {
        return ESP_OK;
    }

    s_gpio_interrupt_queue = xQueueCreate(GPIO_INTERRUPT_QUEUE_LEN, sizeof(gpio_interrupt_event_t));
    return s_gpio_interrupt_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t gpio_interrupt_ensure_isr_service(void)
{
    esp_err_t err;

    if (s_gpio_isr_service_installed) {
        return ESP_OK;
    }

    err = gpio_install_isr_service(0);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        s_gpio_isr_service_installed = true;
    }
    return err;
}

static void gpio_interrupt_clear_callback(JSContext *ctx, gpio_interrupt_slot_t *slot)
{
    if (slot->callback_registered) {
        JS_DeleteGCRef(ctx, &slot->callback);
        slot->callback_registered = false;
    }
}

static esp_err_t gpio_interrupt_release_slot(JSContext *ctx,
                                             gpio_num_t pin,
                                             const char **failed_api_name)
{
    gpio_interrupt_slot_t *slot = &s_gpio_interrupt_slots[pin];
    esp_err_t err = ESP_OK;

    portENTER_CRITICAL(&s_gpio_interrupt_lock);
    slot->attached = false;
    slot->generation++;
    slot->intr_type = GPIO_INTR_DISABLE;
    slot->dropped = 0;
    portEXIT_CRITICAL(&s_gpio_interrupt_lock);

    if (slot->handler_installed && s_gpio_isr_service_installed) {
        err = gpio_intr_disable(pin);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            if (failed_api_name != NULL) {
                *failed_api_name = "gpio_intr_disable";
            }
            return err;
        }

        err = gpio_isr_handler_remove(pin);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            if (failed_api_name != NULL) {
                *failed_api_name = "gpio_isr_handler_remove";
            }
            return err;
        }
    }
    slot->handler_installed = false;

    err = gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
    if (err != ESP_OK) {
        if (failed_api_name != NULL) {
            *failed_api_name = "gpio_set_intr_type";
        }
        return err;
    }

    gpio_interrupt_clear_callback(ctx, slot);
    return ESP_OK;
}

static void gpio_interrupt_isr_handler(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(uintptr_t)arg;
    gpio_interrupt_slot_t *slot;
    gpio_interrupt_event_t event;
    BaseType_t task_woken = pdFALSE;
    bool attached = false;
    uint32_t generation = 0;

    if ((uint32_t)pin >= GPIO_NUM_MAX || s_gpio_interrupt_queue == NULL) {
        return;
    }

    slot = &s_gpio_interrupt_slots[pin];
    portENTER_CRITICAL_ISR(&s_gpio_interrupt_lock);
    attached = slot->attached;
    generation = slot->generation;
    portEXIT_CRITICAL_ISR(&s_gpio_interrupt_lock);
    if (!attached) {
        return;
    }

    event.pin = pin;
    event.generation = generation;
    if (xQueueSendFromISR(s_gpio_interrupt_queue, &event, &task_woken) != pdTRUE) {
        portENTER_CRITICAL_ISR(&s_gpio_interrupt_lock);
        if (slot->attached && slot->generation == generation) {
            slot->dropped++;
        }
        portEXIT_CRITICAL_ISR(&s_gpio_interrupt_lock);
        return;
    }

    esp32_mquickjs_notify_active_runtime_from_isr((int *)&task_woken);
    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static JSValue gpio_make_interrupt_event(JSContext *ctx, gpio_num_t pin, gpio_int_type_t intr_type)
{
    JSGCRef event_ref;
    JSValue *event_obj;

    event_obj = JS_PushGCRef(ctx, &event_ref);
    *event_obj = JS_NewObject(ctx);
    if (JS_IsException(*event_obj)) {
        JS_PopGCRef(ctx, &event_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, event_obj, "pin", JS_NewInt32(ctx, (int32_t)pin)) ||
        !esp32_mquickjs_set_property_ref(ctx, event_obj, "level",
                                     JS_NewBool(gpio_get_level(pin) != 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, event_obj, "mode",
                                     JS_NewString(ctx, gpio_interrupt_mode_to_string(intr_type)))) {
        JS_PopGCRef(ctx, &event_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &event_ref);
}

static bool gpio_interrupt_async_poller(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime,
                                        void *opaque)
{
    gpio_interrupt_event_t event;
    bool needs_redraw = false;

    (void)opaque;
    (void)runtime;
    if (ctx == NULL || s_gpio_interrupt_queue == NULL) {
        return false;
    }

    while (xQueueReceive(s_gpio_interrupt_queue, &event, 0) == pdTRUE) {
        gpio_interrupt_slot_t *slot;
        JSGCRef callback_ref;
        JSValue *callback_fn;
        JSValue callback_result;
        JSValue argv[1];
        bool attached;
        uint32_t generation;
        gpio_int_type_t intr_type;

        if ((uint32_t)event.pin >= GPIO_NUM_MAX) {
            continue;
        }

        slot = &s_gpio_interrupt_slots[event.pin];
        portENTER_CRITICAL(&s_gpio_interrupt_lock);
        attached = slot->attached;
        generation = slot->generation;
        intr_type = slot->intr_type;
        portEXIT_CRITICAL(&s_gpio_interrupt_lock);

        if (!attached || !slot->callback_registered || generation != event.generation) {
            continue;
        }

        if (JS_StackCheck(ctx, 4)) {
            ESP_LOGW(TAG, "Skipping GPIO interrupt callback due to JS stack pressure");
            needs_redraw = true;
            continue;
        }

        callback_fn = JS_PushGCRef(ctx, &callback_ref);
        *callback_fn = slot->callback.val;

        argv[0] = gpio_make_interrupt_event(ctx, event.pin, intr_type);
        if (JS_IsException(argv[0])) {
            esp32_mquickjs_print_exception(ctx);
            JS_PopGCRef(ctx, &callback_ref);
            needs_redraw = true;
            continue;
        }

        callback_result = gpio_call_function(ctx, *callback_fn, JS_NULL, 1, argv);
        if (JS_IsException(callback_result)) {
            esp32_mquickjs_print_exception(ctx);
            needs_redraw = true;
        }

        JS_PopGCRef(ctx, &callback_ref);
    }

    return needs_redraw;
}

static JSValue gpio_make_status(JSContext *ctx, gpio_num_t pin)
{
    JSGCRef status_ref;
    JSValue *status_obj;
    gpio_io_config_t io_config = {0};
    gpio_interrupt_slot_t *slot = &s_gpio_interrupt_slots[pin];
    gpio_int_type_t intr_type;
    uint32_t interrupt_dropped;
    bool interrupt_attached;
    esp_err_t err = gpio_get_io_config(pin, &io_config);

    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_get_io_config", pin);
    }

    portENTER_CRITICAL(&s_gpio_interrupt_lock);
    intr_type = slot->intr_type;
    interrupt_dropped = slot->dropped;
    interrupt_attached = slot->attached;
    portEXIT_CRITICAL(&s_gpio_interrupt_lock);

    status_obj = JS_PushGCRef(ctx, &status_ref);
    *status_obj = JS_NewObject(ctx);
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "pin", JS_NewInt32(ctx, (int32_t)pin)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "valid", JS_NewBool(true)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "outputCapable",
                                     JS_NewBool(GPIO_IS_VALID_OUTPUT_GPIO(pin))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "mode",
                                     JS_NewString(ctx, gpio_mode_to_string(io_config.ie, io_config.oe, io_config.od))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "pull",
                                     JS_NewString(ctx, gpio_pull_to_string(io_config.pu, io_config.pd))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "level",
                                     JS_NewBool(gpio_get_level(pin) != 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "inputEnabled",
                                     JS_NewBool(io_config.ie)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "outputEnabled",
                                     JS_NewBool(io_config.oe)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "openDrain",
                                     JS_NewBool(io_config.od)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "pullup",
                                     JS_NewBool(io_config.pu)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "pulldown",
                                     JS_NewBool(io_config.pd)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "driveStrength",
                                     JS_NewInt32(ctx, (int32_t)io_config.drv)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "held",
                                     JS_NewBool(s_gpio_hold_state[pin])) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "functionSelect",
                                     JS_NewUint32(ctx, io_config.fun_sel)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "signalOut",
                                     JS_NewUint32(ctx, io_config.sig_out)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "outputControlledByPeripheral",
                                     JS_NewBool(io_config.oe_ctrl_by_periph)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "outputEnableInverted",
                                     JS_NewBool(io_config.oe_inv)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "sleepEnabled",
                                     JS_NewBool(io_config.slp_sel)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "interruptAttached",
                                     JS_NewBool(interrupt_attached)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "interruptMode",
                                     interrupt_attached
                                         ? JS_NewString(ctx, gpio_interrupt_mode_to_string(intr_type))
                                         : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "interruptDropped",
                                     JS_NewUint32(ctx, interrupt_dropped))) {
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

JSValue js_gpio_attachInterrupt(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    gpio_interrupt_slot_t *slot;
    gpio_num_t pin;
    gpio_int_type_t intr_type = GPIO_INTR_ANYEDGE;
    JSValue *callback_value;
    const char *failed_api_name = NULL;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0 || !JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx,
                                 "gpio.attachInterrupt(pin, callback, mode?) expects a valid GPIO and function");
    }
    if (argc >= 3 && !JS_IsUndefined(argv[2]) &&
        js_value_to_gpio_interrupt_mode(ctx, argv[2], &intr_type) != 0) {
        return JS_ThrowTypeError(ctx,
                                 "gpio.attachInterrupt(pin, callback, mode?) expects change/rising/falling/low/high");
    }
    if (runtime == NULL) {
        return JS_ThrowInternalError(ctx, "gpio.attachInterrupt() requires an active runtime");
    }

    err = gpio_interrupt_ensure_queue();
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "gpio.attachInterrupt() failed to allocate queue: %s",
                                     esp_err_to_name(err));
    }
    err = gpio_interrupt_ensure_isr_service();
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "gpio.attachInterrupt() failed to install ISR service: %s",
                                     esp_err_to_name(err));
    }
    if (!esp32_mquickjs_register_async_poller(runtime, gpio_interrupt_async_poller, NULL)) {
        return JS_ThrowInternalError(ctx, "gpio.attachInterrupt() failed to register async poller");
    }

    err = gpio_interrupt_release_slot(ctx, pin, &failed_api_name);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx,
                                err,
                                failed_api_name != NULL ? failed_api_name : "gpio_interrupt_release_slot",
                                pin);
    }

    err = gpio_input_enable(pin);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_input_enable", pin);
    }

    err = gpio_set_intr_type(pin, intr_type);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx, err, "gpio_set_intr_type", pin);
    }

    slot = &s_gpio_interrupt_slots[pin];
    callback_value = JS_AddGCRef(ctx, &slot->callback);
    *callback_value = argv[1];
    slot->callback_registered = true;

    portENTER_CRITICAL(&s_gpio_interrupt_lock);
    slot->generation++;
    slot->intr_type = intr_type;
    slot->dropped = 0;
    slot->attached = true;
    portEXIT_CRITICAL(&s_gpio_interrupt_lock);

    err = gpio_isr_handler_add(pin, gpio_interrupt_isr_handler, (void *)(uintptr_t)pin);
    if (err != ESP_OK) {
        gpio_interrupt_release_slot(ctx, pin, NULL);
        return gpio_throw_error(ctx, err, "gpio_isr_handler_add", pin);
    }
    slot->handler_installed = true;

    return gpio_make_status(ctx, pin);
}

JSValue js_gpio_detachInterrupt(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    const char *failed_api_name = NULL;
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.detachInterrupt(pin) expects a valid GPIO");
    }

    err = gpio_interrupt_release_slot(ctx, pin, &failed_api_name);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx,
                                err,
                                failed_api_name != NULL ? failed_api_name : "gpio.detachInterrupt",
                                pin);
    }

    return gpio_make_status(ctx, pin);
}

JSValue js_gpio_reset(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    gpio_num_t pin;
    const char *failed_api_name = NULL;
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_gpio_num(ctx, argv[0], &pin) != 0) {
        return JS_ThrowTypeError(ctx, "gpio.reset(pin) expects a valid GPIO");
    }

    err = gpio_interrupt_release_slot(ctx, pin, &failed_api_name);
    if (err != ESP_OK) {
        return gpio_throw_error(ctx,
                                err,
                                failed_api_name != NULL ? failed_api_name : "gpio_interrupt_release_slot",
                                pin);
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

void esp32_mquickjs_deinit_gpio_runtime(JSContext *ctx)
{
    int pin;

    for (pin = 0; pin < GPIO_NUM_MAX; ++pin) {
        gpio_interrupt_slot_t *slot = &s_gpio_interrupt_slots[pin];

        if (!GPIO_IS_VALID_GPIO(pin) ||
            (!slot->attached && !slot->handler_installed &&
             !slot->callback_registered)) {
            continue;
        }
        (void)gpio_interrupt_release_slot(ctx, (gpio_num_t)pin, NULL);
        gpio_interrupt_clear_callback(ctx, slot);
        slot->attached = false;
        slot->handler_installed = false;
    }

    if (s_gpio_interrupt_queue != NULL) {
        QueueHandle_t queue = s_gpio_interrupt_queue;

        s_gpio_interrupt_queue = NULL;
        vQueueDelete(queue);
    }
}

#endif
