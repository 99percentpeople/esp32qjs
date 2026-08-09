#include "esp32_mquickjs_adc.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_ADC

#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "soc/soc_caps.h"

typedef struct {
    bool configured;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
    adc_cali_handle_t cali_handle;
    adc_cali_scheme_ver_t cali_scheme;
} esp32_mquickjs_adc_channel_state_t;

typedef struct {
    bool opened;
    adc_oneshot_unit_handle_t handle;
    esp32_mquickjs_adc_channel_state_t channels[SOC_ADC_MAX_CHANNEL_NUM];
} esp32_mquickjs_adc_unit_state_t;

static esp32_mquickjs_adc_unit_state_t s_adc_units[SOC_ADC_PERIPH_NUM];
static adc_cali_scheme_ver_t s_adc_cali_schemes;

static int adc_unit_to_number(adc_unit_t unit)
{
    return (int)unit + 1;
}

static bool adc_unit_supported(adc_unit_t unit)
{
    return (int)unit >= 0 && (int)unit < SOC_ADC_PERIPH_NUM;
}

static int adc_channel_count(adc_unit_t unit)
{
    return adc_unit_supported(unit) ? SOC_ADC_CHANNEL_NUM(unit) : 0;
}

static int js_value_to_adc_unit(JSContext *ctx, JSValue value, adc_unit_t *out_unit)
{
    int raw_unit = 0;

    if (JS_ToInt32(ctx, &raw_unit, value) != 0) {
        return -1;
    }
    if (raw_unit != 1 && raw_unit != 2) {
        return -1;
    }

    *out_unit = raw_unit == 1 ? ADC_UNIT_1 : ADC_UNIT_2;
    return adc_unit_supported(*out_unit) ? 0 : -1;
}

static int js_value_to_adc_channel(JSContext *ctx,
                                   JSValue value,
                                   adc_unit_t unit,
                                   adc_channel_t *out_channel)
{
    int raw_channel = -1;

    if (JS_ToInt32(ctx, &raw_channel, value) != 0 ||
        raw_channel < 0 ||
        raw_channel >= adc_channel_count(unit)) {
        return -1;
    }

    *out_channel = (adc_channel_t)raw_channel;
    return 0;
}

static int js_value_to_adc_atten(JSContext *ctx, JSValue value, adc_atten_t *out_atten)
{
    int raw_atten = -1;

    if (JS_ToInt32(ctx, &raw_atten, value) != 0 ||
        raw_atten < ADC_ATTEN_DB_0 ||
        raw_atten > ADC_ATTEN_DB_12) {
        return -1;
    }

    *out_atten = (adc_atten_t)raw_atten;
    return 0;
}

static int js_value_to_adc_bitwidth(JSContext *ctx, JSValue value, adc_bitwidth_t *out_bitwidth)
{
    int raw_bitwidth = -1;

    if (JS_ToInt32(ctx, &raw_bitwidth, value) != 0) {
        return -1;
    }

    if (raw_bitwidth == ADC_BITWIDTH_DEFAULT ||
        raw_bitwidth == ADC_BITWIDTH_9 ||
        raw_bitwidth == ADC_BITWIDTH_10 ||
        raw_bitwidth == ADC_BITWIDTH_11 ||
        raw_bitwidth == ADC_BITWIDTH_12 ||
        raw_bitwidth == ADC_BITWIDTH_13) {
        *out_bitwidth = (adc_bitwidth_t)raw_bitwidth;
        return 0;
    }

    return -1;
}

static esp32_mquickjs_adc_unit_state_t *adc_unit_state(adc_unit_t unit)
{
    return &s_adc_units[(int)unit];
}

static esp32_mquickjs_adc_channel_state_t *adc_channel_state(adc_unit_t unit, adc_channel_t channel)
{
    return &adc_unit_state(unit)->channels[(int)channel];
}

static void adc_release_cali_handle(esp32_mquickjs_adc_channel_state_t *channel_state)
{
    if (channel_state == NULL || channel_state->cali_handle == NULL) {
        return;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (channel_state->cali_scheme == ADC_CALI_SCHEME_VER_CURVE_FITTING) {
        (void)adc_cali_delete_scheme_curve_fitting(channel_state->cali_handle);
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (channel_state->cali_scheme == ADC_CALI_SCHEME_VER_LINE_FITTING) {
        (void)adc_cali_delete_scheme_line_fitting(channel_state->cali_handle);
    }
#endif

    channel_state->cali_handle = NULL;
    channel_state->cali_scheme = 0;
}

static void adc_reset_unit_state(adc_unit_t unit)
{
    esp32_mquickjs_adc_unit_state_t *unit_state = adc_unit_state(unit);

    memset(unit_state, 0, sizeof(*unit_state));
    for (int i = 0; i < SOC_ADC_MAX_CHANNEL_NUM; ++i) {
        unit_state->channels[i].atten = ADC_ATTEN_DB_12;
        unit_state->channels[i].bitwidth = ADC_BITWIDTH_DEFAULT;
    }
}

static void adc_close_unit_state(adc_unit_t unit)
{
    esp32_mquickjs_adc_unit_state_t *unit_state = adc_unit_state(unit);

    for (int i = 0; i < SOC_ADC_MAX_CHANNEL_NUM; ++i) {
        adc_release_cali_handle(&unit_state->channels[i]);
    }

    if (unit_state->handle != NULL) {
        (void)adc_oneshot_del_unit(unit_state->handle);
    }

    adc_reset_unit_state(unit);
}

static JSValue adc_throw_error(JSContext *ctx,
                               esp_err_t err,
                               const char *api_name,
                               adc_unit_t unit,
                               int channel)
{
    if (channel >= 0) {
        return JS_ThrowInternalError(ctx,
                                     "%s(unit=%d, channel=%d) failed: %s",
                                     api_name,
                                     adc_unit_to_number(unit),
                                     channel,
                                     esp_err_to_name(err));
    }

    return JS_ThrowInternalError(ctx,
                                 "%s(unit=%d) failed: %s",
                                 api_name,
                                 adc_unit_to_number(unit),
                                 esp_err_to_name(err));
}

static bool adc_require_open(JSContext *ctx, adc_unit_t unit)
{
    if (!adc_unit_state(unit)->opened || adc_unit_state(unit)->handle == NULL) {
        JS_ThrowInternalError(ctx, "adc unit %d is not open; call adc.open(%d) first", adc_unit_to_number(unit), adc_unit_to_number(unit));
        return false;
    }
    return true;
}

static bool adc_require_configured(JSContext *ctx, adc_unit_t unit, adc_channel_t channel)
{
    if (!adc_channel_state(unit, channel)->configured) {
        JS_ThrowInternalError(ctx,
                              "adc channel is not configured; call adc.configure(%d, %d, ... ) first",
                              adc_unit_to_number(unit),
                              (int)channel);
        return false;
    }
    return true;
}

static void adc_update_cali_state(adc_unit_t unit,
                                  adc_channel_t channel,
                                  adc_atten_t atten,
                                  adc_bitwidth_t bitwidth)
{
    esp32_mquickjs_adc_channel_state_t *channel_state = adc_channel_state(unit, channel);
    adc_cali_handle_t handle = NULL;

    adc_release_cali_handle(channel_state);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if ((s_adc_cali_schemes & ADC_CALI_SCHEME_VER_CURVE_FITTING) != 0) {
        adc_cali_curve_fitting_config_t config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = bitwidth,
        };

        if (adc_cali_create_scheme_curve_fitting(&config, &handle) == ESP_OK) {
            channel_state->cali_handle = handle;
            channel_state->cali_scheme = ADC_CALI_SCHEME_VER_CURVE_FITTING;
            return;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if ((s_adc_cali_schemes & ADC_CALI_SCHEME_VER_LINE_FITTING) != 0) {
        adc_cali_line_fitting_config_t config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = bitwidth,
        };

        if (adc_cali_create_scheme_line_fitting(&config, &handle) == ESP_OK) {
            channel_state->cali_handle = handle;
            channel_state->cali_scheme = ADC_CALI_SCHEME_VER_LINE_FITTING;
        }
    }
#endif
}

static JSValue adc_make_channel_object(JSContext *ctx,
                                       adc_unit_t unit,
                                       adc_channel_t channel)
{
    esp32_mquickjs_adc_channel_state_t *channel_state = adc_channel_state(unit, channel);
    JSGCRef channel_ref;
    JSValue *channel_obj;
    int pin = -1;
    bool mapped = adc_oneshot_channel_to_io(unit, channel, &pin) == ESP_OK;

    channel_obj = JS_PushGCRef(ctx, &channel_ref);
    *channel_obj = JS_NewObject(ctx);
    if (JS_IsException(*channel_obj)) {
        JS_PopGCRef(ctx, &channel_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, channel_obj, "channel", JS_NewInt32(ctx, (int32_t)channel)) ||
        !esp32_mquickjs_set_property_ref(ctx, channel_obj, "configured", JS_NewBool(channel_state->configured)) ||
        !esp32_mquickjs_set_property_ref(ctx, channel_obj, "atten",
                                     channel_state->configured
                                         ? JS_NewInt32(ctx, (int32_t)channel_state->atten)
                                         : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, channel_obj, "bitwidth",
                                     channel_state->configured
                                         ? JS_NewInt32(ctx, (int32_t)channel_state->bitwidth)
                                         : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, channel_obj, "pin",
                                     mapped ? JS_NewInt32(ctx, pin) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, channel_obj, "calibrated",
                                     JS_NewBool(channel_state->cali_handle != NULL))) {
        JS_PopGCRef(ctx, &channel_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &channel_ref);
}

static JSValue adc_make_status_object(JSContext *ctx, adc_unit_t unit)
{
    esp32_mquickjs_adc_unit_state_t *unit_state = adc_unit_state(unit);
    JSGCRef status_ref;
    JSGCRef channels_ref;
    JSValue *status_obj;
    JSValue *channels_array;

    status_obj = JS_PushGCRef(ctx, &status_ref);
    channels_array = JS_PushGCRef(ctx, &channels_ref);
    *status_obj = JS_NewObject(ctx);
    *channels_array = JS_UNDEFINED;
    if (JS_IsException(*status_obj)) {
        goto fail;
    }

    *channels_array = JS_NewArray(ctx, 0);
    if (JS_IsException(*channels_array)) {
        goto fail;
    }

    for (int i = 0; i < adc_channel_count(unit); ++i) {
        JSGCRef channel_ref;
        JSValue *channel_obj = JS_PushGCRef(ctx, &channel_ref);

        *channel_obj = adc_make_channel_object(ctx, unit, (adc_channel_t)i);
        if (JS_IsException(*channel_obj) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *channels_array, (uint32_t)i, *channel_obj))) {
            JS_PopGCRef(ctx, &channel_ref);
            goto fail;
        }
        JS_PopGCRef(ctx, &channel_ref);
    }

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "unit", JS_NewInt32(ctx, adc_unit_to_number(unit))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "opened", JS_NewBool(unit_state->opened)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "channelCount", JS_NewInt32(ctx, adc_channel_count(unit))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "channels", *channels_array)) {
        goto fail;
    }

    JS_PopGCRef(ctx, &channels_ref);
    return JS_PopGCRef(ctx, &status_ref);

fail:
    JS_PopGCRef(ctx, &channels_ref);
    JS_PopGCRef(ctx, &status_ref);
    return JS_EXCEPTION;
}

void esp32_mquickjs_deinit_adc_runtime(void)
{
    for (int unit = 0; unit < SOC_ADC_PERIPH_NUM; ++unit) {
        adc_close_unit_state((adc_unit_t)unit);
    }
    s_adc_cali_schemes = 0;
}

void esp32_mquickjs_init_adc_runtime(void)
{
    esp32_mquickjs_deinit_adc_runtime();
    if (adc_cali_check_scheme(&s_adc_cali_schemes) != ESP_OK) {
        s_adc_cali_schemes = 0;
    }
}

JSValue js_adc_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    adc_unit_t unit;
    adc_oneshot_unit_init_cfg_t config = {0};
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_adc_unit(ctx, argv[0], &unit) != 0) {
        return JS_ThrowTypeError(ctx, "adc.open(unit) expects adc.UNIT_1 or adc.UNIT_2");
    }

    adc_close_unit_state(unit);

    config.unit_id = unit;
    config.clk_src = 0;
    config.ulp_mode = ADC_ULP_MODE_DISABLE;

    err = adc_oneshot_new_unit(&config, &adc_unit_state(unit)->handle);
    if (err != ESP_OK) {
        adc_reset_unit_state(unit);
        return adc_throw_error(ctx, err, "adc_oneshot_new_unit", unit, -1);
    }

    adc_unit_state(unit)->opened = true;
    return adc_make_status_object(ctx, unit);
}

JSValue js_adc_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    adc_unit_t unit;

    (void)this_val;

    if (argc < 1 || js_value_to_adc_unit(ctx, argv[0], &unit) != 0) {
        return JS_ThrowTypeError(ctx, "adc.close(unit) expects adc.UNIT_1 or adc.UNIT_2");
    }

    adc_close_unit_state(unit);
    return JS_NewBool(true);
}

JSValue js_adc_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    adc_unit_t unit;

    (void)this_val;

    if (argc < 1 || js_value_to_adc_unit(ctx, argv[0], &unit) != 0) {
        return JS_ThrowTypeError(ctx, "adc.status(unit) expects adc.UNIT_1 or adc.UNIT_2");
    }

    return adc_make_status_object(ctx, unit);
}

JSValue js_adc_configure(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    adc_unit_t unit;
    adc_channel_t channel;
    adc_atten_t atten = ADC_ATTEN_DB_12;
    adc_bitwidth_t bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_chan_cfg_t config = {0};
    esp_err_t err;

    (void)this_val;

    if (argc < 3 || js_value_to_adc_unit(ctx, argv[0], &unit) != 0) {
        return JS_ThrowTypeError(ctx, "adc.configure(unit, channel, options) expects adc.UNIT_1 or adc.UNIT_2");
    }
    if (js_value_to_adc_channel(ctx, argv[1], unit, &channel) != 0 || JS_GetClassID(ctx, argv[2]) < 0) {
        return JS_ThrowTypeError(ctx, "adc.configure(unit, channel, options) expects a valid channel and options object");
    }
    if (!adc_require_open(ctx, unit)) {
        return JS_EXCEPTION;
    }

    {
        JSValue property = JS_GetPropertyStr(ctx, argv[2], "atten");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !JS_IsNull(property) && js_value_to_adc_atten(ctx, property, &atten) != 0) {
            return JS_ThrowTypeError(ctx, "adc.configure({ atten }) expects adc.ATTEN_DB_0/2_5/6/12");
        }

        property = JS_GetPropertyStr(ctx, argv[2], "bitwidth");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !JS_IsNull(property) && js_value_to_adc_bitwidth(ctx, property, &bitwidth) != 0) {
            return JS_ThrowTypeError(ctx, "adc.configure({ bitwidth }) expects adc.BITWIDTH_DEFAULT or adc.BITWIDTH_9..13");
        }
    }

    config.atten = atten;
    config.bitwidth = bitwidth;

    err = adc_oneshot_config_channel(adc_unit_state(unit)->handle, channel, &config);
    if (err != ESP_OK) {
        return adc_throw_error(ctx, err, "adc_oneshot_config_channel", unit, (int)channel);
    }

    adc_channel_state(unit, channel)->configured = true;
    adc_channel_state(unit, channel)->atten = atten;
    adc_channel_state(unit, channel)->bitwidth = bitwidth;
    adc_update_cali_state(unit, channel, atten, bitwidth);

    return adc_make_status_object(ctx, unit);
}

JSValue js_adc_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    adc_unit_t unit;
    adc_channel_t channel;
    int raw = 0;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_adc_unit(ctx, argv[0], &unit) != 0 || js_value_to_adc_channel(ctx, argv[1], unit, &channel) != 0) {
        return JS_ThrowTypeError(ctx, "adc.read(unit, channel) expects a valid unit/channel pair");
    }
    if (!adc_require_open(ctx, unit) || !adc_require_configured(ctx, unit, channel)) {
        return JS_EXCEPTION;
    }

    err = adc_oneshot_read(adc_unit_state(unit)->handle, channel, &raw);
    if (err != ESP_OK) {
        return adc_throw_error(ctx, err, "adc_oneshot_read", unit, (int)channel);
    }

    return JS_NewInt32(ctx, raw);
}

JSValue js_adc_readMilliVolts(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    adc_unit_t unit;
    adc_channel_t channel;
    int millivolts = 0;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_adc_unit(ctx, argv[0], &unit) != 0 || js_value_to_adc_channel(ctx, argv[1], unit, &channel) != 0) {
        return JS_ThrowTypeError(ctx, "adc.readMilliVolts(unit, channel) expects a valid unit/channel pair");
    }
    if (!adc_require_open(ctx, unit) || !adc_require_configured(ctx, unit, channel)) {
        return JS_EXCEPTION;
    }
    if (adc_channel_state(unit, channel)->cali_handle == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "adc.readMilliVolts(%d, %d) calibration unavailable for this channel",
                                     adc_unit_to_number(unit),
                                     (int)channel);
    }

    err = adc_oneshot_get_calibrated_result(adc_unit_state(unit)->handle,
                                            adc_channel_state(unit, channel)->cali_handle,
                                            channel,
                                            &millivolts);
    if (err != ESP_OK) {
        return adc_throw_error(ctx, err, "adc_oneshot_get_calibrated_result", unit, (int)channel);
    }

    return JS_NewInt32(ctx, millivolts);
}

JSValue js_adc_ioToChannel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int pin = -1;
    adc_unit_t unit;
    adc_channel_t channel;
    esp_err_t err;
    JSGCRef mapping_ref;
    JSValue *mapping_obj;

    (void)this_val;

    if (argc < 1 || JS_ToInt32(ctx, &pin, argv[0]) != 0) {
        return JS_ThrowTypeError(ctx, "adc.ioToChannel(pin) expects a GPIO number");
    }

    err = adc_oneshot_io_to_channel(pin, &unit, &channel);
    if (err != ESP_OK) {
        return JS_NULL;
    }

    mapping_obj = JS_PushGCRef(ctx, &mapping_ref);
    *mapping_obj = JS_NewObject(ctx);
    if (JS_IsException(*mapping_obj)) {
        JS_PopGCRef(ctx, &mapping_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, mapping_obj, "unit", JS_NewInt32(ctx, adc_unit_to_number(unit))) ||
        !esp32_mquickjs_set_property_ref(ctx, mapping_obj, "channel", JS_NewInt32(ctx, (int32_t)channel))) {
        JS_PopGCRef(ctx, &mapping_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &mapping_ref);
}

JSValue js_adc_channelToIo(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    adc_unit_t unit;
    adc_channel_t channel;
    int pin = -1;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_adc_unit(ctx, argv[0], &unit) != 0 || js_value_to_adc_channel(ctx, argv[1], unit, &channel) != 0) {
        return JS_ThrowTypeError(ctx, "adc.channelToIo(unit, channel) expects a valid unit/channel pair");
    }

    err = adc_oneshot_channel_to_io(unit, channel, &pin);
    if (err != ESP_OK) {
        return JS_NULL;
    }

    return JS_NewInt32(ctx, pin);
}

JSValue js_adc_get_unit_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, SOC_ADC_PERIPH_NUM);
}

JSValue js_adc_get_max_channel_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, SOC_ADC_MAX_CHANNEL_NUM);
}

#endif
