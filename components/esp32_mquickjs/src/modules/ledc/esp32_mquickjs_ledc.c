#include "esp32_mquickjs_ledc.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_LEDC

#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "soc/soc_caps.h"

typedef struct {
    bool configured;
    bool paused;
    uint32_t freq_hz;
    ledc_timer_bit_t duty_resolution;
    ledc_clk_cfg_t clock;
} esp32_mquickjs_ledc_timer_state_t;

typedef struct {
    bool configured;
    int pin;
    ledc_timer_t timer;
    uint32_t duty;
    uint32_t hpoint;
    bool output_invert;
    ledc_sleep_mode_t sleep_mode;
} esp32_mquickjs_ledc_channel_state_t;

static esp32_mquickjs_ledc_timer_state_t s_ledc_timers[SOC_LEDC_TIMER_NUM];
static esp32_mquickjs_ledc_channel_state_t s_ledc_channels[SOC_LEDC_CHANNEL_NUM];
static bool s_ledc_fade_service_installed;
static bool s_ledc_fade_service_owned;

static int js_value_to_u32(JSContext *ctx, JSValue value, uint32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0 || raw_value < 0) {
        return -1;
    }

    *out_value = (uint32_t)raw_value;
    return 0;
}

static int js_value_to_bool(JSContext *ctx, JSValue value, bool *out_value)
{
    int raw_value = 0;

    if (JS_IsBool(value)) {
        *out_value = (value == JS_TRUE);
        return 0;
    }

    if (JS_ToInt32(ctx, &raw_value, value) != 0) {
        return -1;
    }

    *out_value = raw_value != 0;
    return 0;
}

static int js_value_to_timer(JSContext *ctx, JSValue value, ledc_timer_t *out_timer)
{
    int timer = -1;

    if (JS_ToInt32(ctx, &timer, value) != 0 || timer < 0 || timer >= SOC_LEDC_TIMER_NUM) {
        return -1;
    }

    *out_timer = (ledc_timer_t)timer;
    return 0;
}

static int js_value_to_channel(JSContext *ctx, JSValue value, ledc_channel_t *out_channel)
{
    int channel = -1;

    if (JS_ToInt32(ctx, &channel, value) != 0 || channel < 0 || channel >= SOC_LEDC_CHANNEL_NUM) {
        return -1;
    }

    *out_channel = (ledc_channel_t)channel;
    return 0;
}

static int js_value_to_gpio_num(JSContext *ctx, JSValue value, gpio_num_t *out_pin)
{
    int pin = -1;

    if (JS_ToInt32(ctx, &pin, value) != 0 ||
        pin < 0 ||
        pin >= GPIO_NUM_MAX ||
        !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        return -1;
    }

    *out_pin = (gpio_num_t)pin;
    return 0;
}

static int js_value_to_duty_resolution(JSContext *ctx, JSValue value, ledc_timer_bit_t *out_resolution)
{
    int bits = 0;

    if (JS_ToInt32(ctx, &bits, value) != 0 ||
        bits < LEDC_TIMER_1_BIT ||
        bits > SOC_LEDC_TIMER_BIT_WIDTH) {
        return -1;
    }

    *out_resolution = (ledc_timer_bit_t)bits;
    return 0;
}

static const char *ledc_clock_to_string(ledc_clk_cfg_t clock)
{
    if (clock == LEDC_AUTO_CLK) {
        return "auto";
    }
#if SOC_LEDC_SUPPORT_APB_CLOCK
    if (clock == LEDC_USE_APB_CLK) {
        return "apb";
    }
#endif
#if SOC_LEDC_SUPPORT_XTAL_CLOCK
    if (clock == LEDC_USE_XTAL_CLK) {
        return "xtal";
    }
#endif
    if (clock == LEDC_USE_RC_FAST_CLK) {
        return "rcFast";
    }
    return "auto";
}

static int ledc_clock_from_string(const char *clock_name, ledc_clk_cfg_t *out_clock)
{
    if (strcmp(clock_name, "auto") == 0) {
        *out_clock = LEDC_AUTO_CLK;
        return 0;
    }
#if SOC_LEDC_SUPPORT_APB_CLOCK
    if (strcmp(clock_name, "apb") == 0) {
        *out_clock = LEDC_USE_APB_CLK;
        return 0;
    }
#endif
#if SOC_LEDC_SUPPORT_XTAL_CLOCK
    if (strcmp(clock_name, "xtal") == 0) {
        *out_clock = LEDC_USE_XTAL_CLK;
        return 0;
    }
#endif
    if (strcmp(clock_name, "rcFast") == 0 || strcmp(clock_name, "rc_fast") == 0) {
        *out_clock = LEDC_USE_RC_FAST_CLK;
        return 0;
    }

    return -1;
}

static const char *ledc_sleep_mode_to_string(ledc_sleep_mode_t sleep_mode)
{
    if (sleep_mode == LEDC_SLEEP_MODE_NO_ALIVE_ALLOW_PD) {
        return "noAliveAllowPd";
    }
    if (sleep_mode == LEDC_SLEEP_MODE_KEEP_ALIVE) {
        return "keepAlive";
    }
    return "noAliveNoPd";
}

static int ledc_sleep_mode_from_string(const char *sleep_mode, ledc_sleep_mode_t *out_sleep_mode)
{
    if (strcmp(sleep_mode, "noAliveNoPd") == 0 || strcmp(sleep_mode, "no_alive_no_pd") == 0) {
        *out_sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
        return 0;
    }
    if (strcmp(sleep_mode, "noAliveAllowPd") == 0 || strcmp(sleep_mode, "no_alive_allow_pd") == 0) {
        *out_sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_ALLOW_PD;
        return 0;
    }
    if (strcmp(sleep_mode, "keepAlive") == 0 || strcmp(sleep_mode, "keep_alive") == 0) {
        *out_sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE;
        return 0;
    }

    return -1;
}

static uint32_t ledc_resolution_to_max_duty(ledc_timer_bit_t duty_resolution)
{
    if (duty_resolution <= 0 || duty_resolution >= 31) {
        return 0;
    }
    return (1u << duty_resolution) - 1u;
}

static esp32_mquickjs_ledc_timer_state_t *ledc_timer_state(ledc_timer_t timer)
{
    return &s_ledc_timers[(int)timer];
}

static esp32_mquickjs_ledc_channel_state_t *ledc_channel_state(ledc_channel_t channel)
{
    return &s_ledc_channels[(int)channel];
}

static JSValue ledc_throw_error(JSContext *ctx,
                                esp_err_t err,
                                const char *api_name,
                                int index)
{
    return JS_ThrowInternalError(ctx, "%s(%d) failed: %s", api_name, index, esp_err_to_name(err));
}

static bool ledc_ensure_fade_service(JSContext *ctx)
{
    esp_err_t err;

    if (s_ledc_fade_service_installed) {
        return true;
    }

    err = ledc_fade_func_install(0);
    if (err == ESP_OK) {
        s_ledc_fade_service_installed = true;
        s_ledc_fade_service_owned = true;
        return true;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        s_ledc_fade_service_installed = true;
        s_ledc_fade_service_owned = false;
        return true;
    }

    JS_ThrowInternalError(ctx, "ledc_fade_func_install() failed: %s", esp_err_to_name(err));
    return false;
}

static JSValue ledc_make_timer_status(JSContext *ctx, ledc_timer_t timer)
{
    esp32_mquickjs_ledc_timer_state_t *state = ledc_timer_state(timer);
    JSGCRef status_ref;
    JSValue *status_obj;

    status_obj = JS_PushGCRef(ctx, &status_ref);
    *status_obj = JS_NewObject(ctx);
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "timer", JS_NewInt32(ctx, (int32_t)timer)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "configured", JS_NewBool(state->configured)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "paused", JS_NewBool(state->paused)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "freqHz", JS_NewUint32(ctx, state->configured ? state->freq_hz : 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "dutyResolution",
                                     JS_NewInt32(ctx, state->configured ? (int32_t)state->duty_resolution : 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "maxDuty",
                                     JS_NewUint32(ctx, state->configured
                                                           ? ledc_resolution_to_max_duty(state->duty_resolution)
                                                           : 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "clock",
                                     JS_NewString(ctx, ledc_clock_to_string(state->clock)))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &status_ref);
}

static JSValue ledc_make_channel_status(JSContext *ctx, ledc_channel_t channel)
{
    esp32_mquickjs_ledc_channel_state_t *state = ledc_channel_state(channel);
    esp32_mquickjs_ledc_timer_state_t *timer_state = state->configured ? ledc_timer_state(state->timer) : NULL;
    JSGCRef status_ref;
    JSValue *status_obj;

    status_obj = JS_PushGCRef(ctx, &status_ref);
    *status_obj = JS_NewObject(ctx);
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "channel", JS_NewInt32(ctx, (int32_t)channel)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "configured", JS_NewBool(state->configured)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "pin", JS_NewInt32(ctx, (int32_t)state->pin)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "timer",
                                     JS_NewInt32(ctx, state->configured ? (int32_t)state->timer : -1)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "duty",
                                     JS_NewUint32(ctx, state->configured ? state->duty : 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "hpoint",
                                     JS_NewUint32(ctx, state->configured ? state->hpoint : 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "maxDuty",
                                     JS_NewUint32(ctx, state->configured && timer_state != NULL && timer_state->configured
                                                           ? ledc_resolution_to_max_duty(timer_state->duty_resolution)
                                                           : 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "outputInvert",
                                     JS_NewBool(state->configured && state->output_invert)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "sleepMode",
                                     JS_NewString(ctx, ledc_sleep_mode_to_string(state->sleep_mode)))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &status_ref);
}

static bool ledc_read_bool_option(JSContext *ctx,
                                  JSValue options,
                                  const char *name,
                                  bool *out_value)
{
    JSValue property = JS_GetPropertyStr(ctx, options, name);

    if (JS_IsException(property)) {
        return false;
    }
    if (JS_IsUndefined(property) || JS_IsNull(property)) {
        return true;
    }
    if (js_value_to_bool(ctx, property, out_value) != 0) {
        JS_ThrowTypeError(ctx, "ledc.%s expects a boolean-like value", name);
        return false;
    }
    return true;
}

void esp32_mquickjs_deinit_ledc_runtime(void)
{
    for (size_t i = 0; i < SOC_LEDC_CHANNEL_NUM; ++i) {
        if (s_ledc_channels[i].configured) {
            ledc_channel_config_t config = {
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel = (ledc_channel_t)i,
                .deconfigure = true,
            };

            (void)ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i, 0);
            (void)ledc_channel_config(&config);
        }
    }
    for (size_t i = 0; i < SOC_LEDC_TIMER_NUM; ++i) {
        if (s_ledc_timers[i].configured) {
            ledc_timer_config_t config = {
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .timer_num = (ledc_timer_t)i,
                .deconfigure = true,
            };

            (void)ledc_timer_pause(LEDC_LOW_SPEED_MODE, (ledc_timer_t)i);
            (void)ledc_timer_config(&config);
        }
    }
    if (s_ledc_fade_service_owned) {
        ledc_fade_func_uninstall();
    }

    memset(s_ledc_timers, 0, sizeof(s_ledc_timers));
    memset(s_ledc_channels, 0, sizeof(s_ledc_channels));
    s_ledc_fade_service_installed = false;
    s_ledc_fade_service_owned = false;

    for (size_t i = 0; i < SOC_LEDC_CHANNEL_NUM; ++i) {
        s_ledc_channels[i].pin = -1;
        s_ledc_channels[i].timer = (ledc_timer_t)-1;
        s_ledc_channels[i].sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
    }
    for (size_t i = 0; i < SOC_LEDC_TIMER_NUM; ++i) {
        s_ledc_timers[i].clock = LEDC_AUTO_CLK;
    }
}

void esp32_mquickjs_init_ledc_runtime(void)
{
    esp32_mquickjs_deinit_ledc_runtime();
}

JSValue js_ledc_timerConfig(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_timer_t timer;
    bool deconfigure = false;
    uint32_t freq_hz = 0;
    ledc_timer_bit_t duty_resolution = LEDC_TIMER_8_BIT;
    ledc_clk_cfg_t clock = LEDC_AUTO_CLK;
    ledc_timer_config_t config = {0};
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_timer(ctx, argv[0], &timer) != 0 || JS_GetClassID(ctx, argv[1]) < 0) {
        return JS_ThrowTypeError(ctx,
                                 "ledc.timerConfig(timer, options) expects a timer index and an options object");
    }

    if (!ledc_read_bool_option(ctx, argv[1], "deconfigure", &deconfigure)) {
        return JS_EXCEPTION;
    }

    config.speed_mode = LEDC_LOW_SPEED_MODE;
    config.timer_num = timer;
    config.deconfigure = deconfigure;

    if (!deconfigure) {
        JSValue property = JS_GetPropertyStr(ctx, argv[1], "freqHz");
        if (JS_IsException(property) || js_value_to_u32(ctx, property, &freq_hz) != 0 || freq_hz == 0) {
            return JS_ThrowTypeError(ctx, "ledc.timerConfig({ freqHz }) expects a positive integer");
        }

        property = JS_GetPropertyStr(ctx, argv[1], "dutyResolution");
        if (JS_IsException(property) || js_value_to_duty_resolution(ctx, property, &duty_resolution) != 0) {
            return JS_ThrowTypeError(ctx, "ledc.timerConfig({ dutyResolution }) expects 1..%d", SOC_LEDC_TIMER_BIT_WIDTH);
        }

        property = JS_GetPropertyStr(ctx, argv[1], "clock");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !JS_IsNull(property)) {
            JSCStringBuf clock_buf;
            const char *clock_name = JS_ToCString(ctx, property, &clock_buf);

            if (clock_name == NULL || ledc_clock_from_string(clock_name, &clock) != 0) {
                return JS_ThrowTypeError(ctx,
                                         "ledc.timerConfig({ clock }) expects \"auto\", \"apb\", \"xtal\", or \"rcFast\"");
            }
        }

        config.freq_hz = freq_hz;
        config.duty_resolution = duty_resolution;
        config.clk_cfg = clock;
    } else if (ledc_timer_state(timer)->configured) {
        (void)ledc_timer_pause(LEDC_LOW_SPEED_MODE, timer);
    }

    err = ledc_timer_config(&config);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_timer_config", (int)timer);
    }

    if (deconfigure) {
        memset(ledc_timer_state(timer), 0, sizeof(*ledc_timer_state(timer)));
        ledc_timer_state(timer)->clock = LEDC_AUTO_CLK;
    } else {
        ledc_timer_state(timer)->configured = true;
        ledc_timer_state(timer)->paused = false;
        ledc_timer_state(timer)->freq_hz = freq_hz;
        ledc_timer_state(timer)->duty_resolution = duty_resolution;
        ledc_timer_state(timer)->clock = clock;
    }

    return ledc_make_timer_status(ctx, timer);
}

JSValue js_ledc_channelConfig(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    bool deconfigure = false;
    gpio_num_t pin = GPIO_NUM_NC;
    ledc_timer_t timer = LEDC_TIMER_0;
    uint32_t duty = 0;
    uint32_t hpoint = 0;
    bool output_invert = false;
    ledc_sleep_mode_t sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
    ledc_channel_config_t config = {0};
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_channel(ctx, argv[0], &channel) != 0 || JS_GetClassID(ctx, argv[1]) < 0) {
        return JS_ThrowTypeError(ctx,
                                 "ledc.channelConfig(channel, options) expects a channel index and an options object");
    }

    if (!ledc_read_bool_option(ctx, argv[1], "deconfigure", &deconfigure)) {
        return JS_EXCEPTION;
    }

    config.speed_mode = LEDC_LOW_SPEED_MODE;
    config.channel = channel;
    config.deconfigure = deconfigure;

    if (!deconfigure) {
        JSValue property = JS_GetPropertyStr(ctx, argv[1], "pin");
        if (JS_IsException(property) || js_value_to_gpio_num(ctx, property, &pin) != 0) {
            return JS_ThrowTypeError(ctx, "ledc.channelConfig({ pin }) expects an output-capable GPIO");
        }

        property = JS_GetPropertyStr(ctx, argv[1], "timer");
        if (JS_IsException(property) || js_value_to_timer(ctx, property, &timer) != 0) {
            return JS_ThrowTypeError(ctx, "ledc.channelConfig({ timer }) expects a valid timer index");
        }

        property = JS_GetPropertyStr(ctx, argv[1], "duty");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !JS_IsNull(property)) {
            if (js_value_to_u32(ctx, property, &duty) != 0) {
                return JS_ThrowTypeError(ctx, "ledc.channelConfig({ duty }) expects a non-negative integer");
            }
        }

        property = JS_GetPropertyStr(ctx, argv[1], "hpoint");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !JS_IsNull(property)) {
            if (js_value_to_u32(ctx, property, &hpoint) != 0) {
                return JS_ThrowTypeError(ctx, "ledc.channelConfig({ hpoint }) expects a non-negative integer");
            }
        }

        if (!ledc_read_bool_option(ctx, argv[1], "outputInvert", &output_invert)) {
            return JS_EXCEPTION;
        }

        property = JS_GetPropertyStr(ctx, argv[1], "sleepMode");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !JS_IsNull(property)) {
            JSCStringBuf sleep_buf;
            const char *sleep_name = JS_ToCString(ctx, property, &sleep_buf);

            if (sleep_name == NULL || ledc_sleep_mode_from_string(sleep_name, &sleep_mode) != 0) {
                return JS_ThrowTypeError(ctx,
                                         "ledc.channelConfig({ sleepMode }) expects \"noAliveNoPd\", \"noAliveAllowPd\", or \"keepAlive\"");
            }
        }

        config.gpio_num = pin;
        config.timer_sel = timer;
        config.duty = duty;
        config.hpoint = (int)hpoint;
        config.sleep_mode = sleep_mode;
        config.flags.output_invert = output_invert ? 1U : 0U;
    }

    err = ledc_channel_config(&config);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_channel_config", (int)channel);
    }

    if (deconfigure) {
        memset(ledc_channel_state(channel), 0, sizeof(*ledc_channel_state(channel)));
        ledc_channel_state(channel)->pin = -1;
        ledc_channel_state(channel)->timer = (ledc_timer_t)-1;
        ledc_channel_state(channel)->sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
    } else {
        ledc_channel_state(channel)->configured = true;
        ledc_channel_state(channel)->pin = (int)pin;
        ledc_channel_state(channel)->timer = timer;
        ledc_channel_state(channel)->duty = duty;
        ledc_channel_state(channel)->hpoint = hpoint;
        ledc_channel_state(channel)->output_invert = output_invert;
        ledc_channel_state(channel)->sleep_mode = sleep_mode;
    }

    return ledc_make_channel_status(ctx, channel);
}

JSValue js_ledc_setDuty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    uint32_t duty = 0;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_channel(ctx, argv[0], &channel) != 0 || js_value_to_u32(ctx, argv[1], &duty) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.setDuty(channel, duty) expects a channel index and a non-negative duty");
    }

    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_set_duty", (int)channel);
    }

    ledc_channel_state(channel)->duty = duty;
    return ledc_make_channel_status(ctx, channel);
}

JSValue js_ledc_setDutyWithHpoint(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    uint32_t duty = 0;
    uint32_t hpoint = 0;
    esp_err_t err;

    (void)this_val;

    if (argc < 3 ||
        js_value_to_channel(ctx, argv[0], &channel) != 0 ||
        js_value_to_u32(ctx, argv[1], &duty) != 0 ||
        js_value_to_u32(ctx, argv[2], &hpoint) != 0) {
        return JS_ThrowTypeError(ctx,
                                 "ledc.setDutyWithHpoint(channel, duty, hpoint) expects non-negative integers");
    }

    err = ledc_set_duty_with_hpoint(LEDC_LOW_SPEED_MODE, channel, duty, hpoint);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_set_duty_with_hpoint", (int)channel);
    }

    ledc_channel_state(channel)->duty = duty;
    ledc_channel_state(channel)->hpoint = hpoint;
    return ledc_make_channel_status(ctx, channel);
}

JSValue js_ledc_setDutyAndUpdate(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    uint32_t duty = 0;
    uint32_t hpoint = 0;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 ||
        js_value_to_channel(ctx, argv[0], &channel) != 0 ||
        js_value_to_u32(ctx, argv[1], &duty) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.setDutyAndUpdate(channel, duty, hpoint?) expects numeric arguments");
    }

    if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]) && js_value_to_u32(ctx, argv[2], &hpoint) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.setDutyAndUpdate(channel, duty, hpoint?) expects a non-negative hpoint");
    }
    if (!ledc_ensure_fade_service(ctx)) {
        return JS_EXCEPTION;
    }

    err = ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, channel, duty, hpoint);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_set_duty_and_update", (int)channel);
    }

    ledc_channel_state(channel)->duty = duty;
    ledc_channel_state(channel)->hpoint = hpoint;
    return ledc_make_channel_status(ctx, channel);
}

JSValue js_ledc_getDuty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    uint32_t duty;

    (void)this_val;

    if (argc < 1 || js_value_to_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.getDuty(channel) expects a channel index");
    }

    duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, channel);
    if (duty == LEDC_ERR_DUTY) {
        return JS_ThrowInternalError(ctx, "ledc.getDuty(%d) failed", (int)channel);
    }

    ledc_channel_state(channel)->duty = duty;
    return JS_NewUint32(ctx, duty);
}

JSValue js_ledc_getHpoint(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    int hpoint;

    (void)this_val;

    if (argc < 1 || js_value_to_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.getHpoint(channel) expects a channel index");
    }

    hpoint = ledc_get_hpoint(LEDC_LOW_SPEED_MODE, channel);
    if (hpoint == LEDC_ERR_VAL) {
        return JS_ThrowInternalError(ctx, "ledc.getHpoint(%d) failed", (int)channel);
    }

    ledc_channel_state(channel)->hpoint = (uint32_t)hpoint;
    return JS_NewInt32(ctx, hpoint);
}

JSValue js_ledc_updateDuty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.updateDuty(channel) expects a channel index");
    }

    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_update_duty", (int)channel);
    }

    return ledc_make_channel_status(ctx, channel);
}

JSValue js_ledc_setFreq(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_timer_t timer;
    uint32_t freq_hz = 0;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_timer(ctx, argv[0], &timer) != 0 || js_value_to_u32(ctx, argv[1], &freq_hz) != 0 || freq_hz == 0) {
        return JS_ThrowTypeError(ctx, "ledc.setFreq(timer, freqHz) expects a timer index and a positive frequency");
    }

    err = ledc_set_freq(LEDC_LOW_SPEED_MODE, timer, freq_hz);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_set_freq", (int)timer);
    }

    ledc_timer_state(timer)->configured = true;
    ledc_timer_state(timer)->freq_hz = freq_hz;
    return ledc_make_timer_status(ctx, timer);
}

JSValue js_ledc_getFreq(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_timer_t timer;
    uint32_t freq_hz;

    (void)this_val;

    if (argc < 1 || js_value_to_timer(ctx, argv[0], &timer) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.getFreq(timer) expects a timer index");
    }

    freq_hz = ledc_get_freq(LEDC_LOW_SPEED_MODE, timer);
    if (freq_hz == 0) {
        return JS_ThrowInternalError(ctx, "ledc.getFreq(%d) failed", (int)timer);
    }

    ledc_timer_state(timer)->configured = true;
    ledc_timer_state(timer)->freq_hz = freq_hz;
    return JS_NewUint32(ctx, freq_hz);
}

JSValue js_ledc_bindChannelTimer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    ledc_timer_t timer;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_channel(ctx, argv[0], &channel) != 0 || js_value_to_timer(ctx, argv[1], &timer) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.bindChannelTimer(channel, timer) expects valid indices");
    }

    err = ledc_bind_channel_timer(LEDC_LOW_SPEED_MODE, channel, timer);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_bind_channel_timer", (int)channel);
    }

    ledc_channel_state(channel)->configured = true;
    ledc_channel_state(channel)->timer = timer;
    return ledc_make_channel_status(ctx, channel);
}

JSValue js_ledc_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;
    bool idle_level = false;
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.stop(channel, idleLevel?) expects a channel index");
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) && js_value_to_bool(ctx, argv[1], &idle_level) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.stop(channel, idleLevel?) expects a boolean-like idle level");
    }

    err = ledc_stop(LEDC_LOW_SPEED_MODE, channel, idle_level ? 1u : 0u);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_stop", (int)channel);
    }

    return ledc_make_channel_status(ctx, channel);
}

JSValue js_ledc_timerPause(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_timer_t timer;
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_timer(ctx, argv[0], &timer) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.timerPause(timer) expects a timer index");
    }

    err = ledc_timer_pause(LEDC_LOW_SPEED_MODE, timer);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_timer_pause", (int)timer);
    }

    ledc_timer_state(timer)->configured = true;
    ledc_timer_state(timer)->paused = true;
    return ledc_make_timer_status(ctx, timer);
}

JSValue js_ledc_timerResume(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_timer_t timer;
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_timer(ctx, argv[0], &timer) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.timerResume(timer) expects a timer index");
    }

    err = ledc_timer_resume(LEDC_LOW_SPEED_MODE, timer);
    if (err != ESP_OK) {
        return ledc_throw_error(ctx, err, "ledc_timer_resume", (int)timer);
    }

    ledc_timer_state(timer)->configured = true;
    ledc_timer_state(timer)->paused = false;
    return ledc_make_timer_status(ctx, timer);
}

JSValue js_ledc_timerStatus(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_timer_t timer;

    (void)this_val;

    if (argc < 1 || js_value_to_timer(ctx, argv[0], &timer) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.timerStatus(timer) expects a timer index");
    }

    return ledc_make_timer_status(ctx, timer);
}

JSValue js_ledc_channelStatus(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    ledc_channel_t channel;

    (void)this_val;

    if (argc < 1 || js_value_to_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "ledc.channelStatus(channel) expects a channel index");
    }

    return ledc_make_channel_status(ctx, channel);
}

JSValue js_ledc_get_channel_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, SOC_LEDC_CHANNEL_NUM);
}

JSValue js_ledc_get_timer_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, SOC_LEDC_TIMER_NUM);
}

JSValue js_ledc_get_max_duty_resolution_bits(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, SOC_LEDC_TIMER_BIT_WIDTH);
}

#endif
