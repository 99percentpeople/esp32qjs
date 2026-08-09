#include "esp32_mquickjs_dac.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DAC

#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/dac_oneshot.h"
#include "esp_err.h"
#include "hal/dac_periph.h"
#include "soc/soc_caps.h"

typedef struct {
    bool opened;
    uint8_t last_value;
    dac_oneshot_handle_t handle;
} esp32_mquickjs_dac_channel_state_t;

static esp32_mquickjs_dac_channel_state_t s_dac_channels[SOC_DAC_CHAN_NUM];

static bool dac_channel_supported(dac_channel_t channel)
{
    return (int)channel >= 0 && (int)channel < SOC_DAC_CHAN_NUM;
}

static int dac_resolution_bits(void)
{
    return SOC_DAC_RESOLUTION;
}

static uint32_t dac_max_value(void)
{
    return (1u << SOC_DAC_RESOLUTION) - 1u;
}

static int dac_channel_to_pin(dac_channel_t channel)
{
    return dac_channel_supported(channel) ? (int)dac_periph_signal.dac_channel_io_num[(int)channel] : -1;
}

static int js_value_to_dac_channel(JSContext *ctx, JSValue value, dac_channel_t *out_channel)
{
    int raw_channel = -1;

    if (JS_ToInt32(ctx, &raw_channel, value) != 0 ||
        raw_channel < 0 ||
        raw_channel >= SOC_DAC_CHAN_NUM) {
        return -1;
    }

    *out_channel = (dac_channel_t)raw_channel;
    return 0;
}

static int js_value_to_dac_value(JSContext *ctx, JSValue value, uint8_t *out_value)
{
    int raw_value = -1;

    if (JS_ToInt32(ctx, &raw_value, value) != 0 ||
        raw_value < 0 ||
        raw_value > (int)dac_max_value()) {
        return -1;
    }

    *out_value = (uint8_t)raw_value;
    return 0;
}

static esp32_mquickjs_dac_channel_state_t *dac_channel_state(dac_channel_t channel)
{
    return &s_dac_channels[(int)channel];
}

static void dac_reset_channel_state(dac_channel_t channel)
{
    memset(dac_channel_state(channel), 0, sizeof(*dac_channel_state(channel)));
}

static void dac_close_channel_state(dac_channel_t channel)
{
    esp32_mquickjs_dac_channel_state_t *state = dac_channel_state(channel);

    if (state->handle != NULL) {
        (void)dac_oneshot_del_channel(state->handle);
    }

    dac_reset_channel_state(channel);
}

static JSValue dac_throw_error(JSContext *ctx,
                               esp_err_t err,
                               const char *api_name,
                               dac_channel_t channel)
{
    return JS_ThrowInternalError(ctx,
                                 "%s(channel=%d) failed: %s",
                                 api_name,
                                 (int)channel,
                                 esp_err_to_name(err));
}

static bool dac_require_open(JSContext *ctx, dac_channel_t channel)
{
    if (!dac_channel_state(channel)->opened || dac_channel_state(channel)->handle == NULL) {
        JS_ThrowInternalError(ctx,
                              "dac channel %d is not open; call dac.open(%d) first",
                              (int)channel,
                              (int)channel);
        return false;
    }
    return true;
}

static JSValue dac_make_status_object(JSContext *ctx, dac_channel_t channel)
{
    esp32_mquickjs_dac_channel_state_t *state = dac_channel_state(channel);
    JSGCRef status_ref;
    JSValue *status_obj;

    status_obj = JS_PushGCRef(ctx, &status_ref);
    *status_obj = JS_NewObject(ctx);
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "channel", JS_NewInt32(ctx, (int32_t)channel)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "opened", JS_NewBool(state->opened)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "pin", JS_NewInt32(ctx, dac_channel_to_pin(channel))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "resolutionBits", JS_NewInt32(ctx, dac_resolution_bits())) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "maxValue", JS_NewUint32(ctx, dac_max_value())) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "lastValue", JS_NewUint32(ctx, state->last_value))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &status_ref);
}

static JSValue dac_make_status_array(JSContext *ctx)
{
    JSGCRef array_ref;
    JSValue *array_obj;

    array_obj = JS_PushGCRef(ctx, &array_ref);
    *array_obj = JS_NewArray(ctx, 0);
    if (JS_IsException(*array_obj)) {
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }

    for (int i = 0; i < SOC_DAC_CHAN_NUM; ++i) {
        JSGCRef status_ref;
        JSValue *status = JS_PushGCRef(ctx, &status_ref);

        *status = dac_make_status_object(ctx, (dac_channel_t)i);
        if (JS_IsException(*status) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array_obj, (uint32_t)i, *status))) {
            JS_PopGCRef(ctx, &status_ref);
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
        JS_PopGCRef(ctx, &status_ref);
    }

    return JS_PopGCRef(ctx, &array_ref);
}

void esp32_mquickjs_deinit_dac_runtime(void)
{
    for (int i = 0; i < SOC_DAC_CHAN_NUM; ++i) {
        dac_close_channel_state((dac_channel_t)i);
    }
}

void esp32_mquickjs_init_dac_runtime(void)
{
    esp32_mquickjs_deinit_dac_runtime();
}

JSValue js_dac_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    dac_channel_t channel;
    dac_oneshot_config_t config = {0};
    esp_err_t err;

    (void)this_val;

    if (argc < 1 || js_value_to_dac_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "dac.open(channel) expects dac.CHANNEL_0 or dac.CHANNEL_1");
    }

    dac_close_channel_state(channel);
    config.chan_id = channel;

    err = dac_oneshot_new_channel(&config, &dac_channel_state(channel)->handle);
    if (err != ESP_OK) {
        dac_reset_channel_state(channel);
        return dac_throw_error(ctx, err, "dac_oneshot_new_channel", channel);
    }

    dac_channel_state(channel)->opened = true;
    return dac_make_status_object(ctx, channel);
}

JSValue js_dac_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    dac_channel_t channel;

    (void)ctx;
    (void)this_val;

    if (argc < 1 || js_value_to_dac_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "dac.close(channel) expects dac.CHANNEL_0 or dac.CHANNEL_1");
    }

    dac_close_channel_state(channel);
    return JS_TRUE;
}

JSValue js_dac_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    dac_channel_t channel;

    (void)this_val;

    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0])) {
        return dac_make_status_array(ctx);
    }
    if (js_value_to_dac_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "dac.status(channel?) expects dac.CHANNEL_0 or dac.CHANNEL_1");
    }

    return dac_make_status_object(ctx, channel);
}

JSValue js_dac_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    dac_channel_t channel;
    uint8_t value = 0;
    esp_err_t err;

    (void)this_val;

    if (argc < 2 || js_value_to_dac_channel(ctx, argv[0], &channel) != 0 || js_value_to_dac_value(ctx, argv[1], &value) != 0) {
        return JS_ThrowTypeError(ctx, "dac.write(channel, value) expects a valid channel and 0..255 value");
    }
    if (!dac_require_open(ctx, channel)) {
        return JS_EXCEPTION;
    }

    err = dac_oneshot_output_voltage(dac_channel_state(channel)->handle, value);
    if (err != ESP_OK) {
        return dac_throw_error(ctx, err, "dac_oneshot_output_voltage", channel);
    }

    dac_channel_state(channel)->last_value = value;
    return dac_make_status_object(ctx, channel);
}

JSValue js_dac_ioToChannel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int pin = -1;
    JSGCRef mapping_ref;
    JSValue *mapping_obj;

    (void)this_val;

    if (argc < 1 || JS_ToInt32(ctx, &pin, argv[0]) != 0) {
        return JS_ThrowTypeError(ctx, "dac.ioToChannel(pin) expects a GPIO number");
    }

    for (int i = 0; i < SOC_DAC_CHAN_NUM; ++i) {
        if (dac_channel_to_pin((dac_channel_t)i) != pin) {
            continue;
        }

        mapping_obj = JS_PushGCRef(ctx, &mapping_ref);
        *mapping_obj = JS_NewObject(ctx);
        if (JS_IsException(*mapping_obj)) {
            JS_PopGCRef(ctx, &mapping_ref);
            return JS_EXCEPTION;
        }

        if (!esp32_mquickjs_set_property_ref(ctx, mapping_obj, "channel", JS_NewInt32(ctx, i)) ||
            !esp32_mquickjs_set_property_ref(ctx, mapping_obj, "pin", JS_NewInt32(ctx, pin))) {
            JS_PopGCRef(ctx, &mapping_ref);
            return JS_EXCEPTION;
        }

        return JS_PopGCRef(ctx, &mapping_ref);
    }

    return JS_NULL;
}

JSValue js_dac_channelToIo(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    dac_channel_t channel;

    (void)this_val;

    if (argc < 1 || js_value_to_dac_channel(ctx, argv[0], &channel) != 0) {
        return JS_ThrowTypeError(ctx, "dac.channelToIo(channel) expects dac.CHANNEL_0 or dac.CHANNEL_1");
    }

    return JS_NewInt32(ctx, dac_channel_to_pin(channel));
}

JSValue js_dac_get_channel_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, SOC_DAC_CHAN_NUM);
}

JSValue js_dac_get_resolution_bits(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, dac_resolution_bits());
}

JSValue js_dac_get_max_value(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, dac_max_value());
}

#endif
