#include "cutils.h"
#include "esp32_mquickjs_wifi.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_wifi_tx_rate.h"
#include "esp32_mquickjs_wifi_antenna.h"
#include "esp32_mquickjs_wifi_scan_parameters.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp32_mquickjs_wifi_config_observations.inc"
#include <string.h>

static bool tx_rate_interface(JSContext *ctx, JSValue value, wifi_interface_t *interface)
{
    if (!JS_IsString(ctx, value)) goto invalid;
    JSCStringBuf buffer;
    size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (text == NULL) return false;
    if (length == 7 && memcmp(text, "station", 7) == 0) {
        *interface = WIFI_IF_STA;
        return true;
    }
    if (length == 12 && memcmp(text, "access-point", 12) == 0) {
        *interface = WIFI_IF_AP;
        return true;
    }
invalid:
    JS_ThrowTypeError(ctx, "Wi-Fi interface must be station or access-point");
    return false;
}

/* Every getter runs once with rooted options/value; only native scalars escape.
 * Validation finishes before taking the Radio mutation mutex or calling SDK. */
bool esp32_mquickjs_wifi_tx_rate_capture(JSContext *ctx, JSValue input, wifi_tx_rate_config_t *config)
{
    static const char *const keys[] = {"phy", "rate", "ersu", "dcm"};
    static const wifi_phy_mode_t modes[] = {WIFI_PHY_MODE_LR, WIFI_PHY_MODE_11B,
        WIFI_PHY_MODE_11G, WIFI_PHY_MODE_11A, WIFI_PHY_MODE_HT20, WIFI_PHY_MODE_HT40,
        WIFI_PHY_MODE_HE20, WIFI_PHY_MODE_VHT20};
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *options = input;
    *config = (wifi_tx_rate_config_t){0};
    if (!esp32_mquickjs_validate_plain_options(ctx, *options,
        "wifi.driver.configureTxRate", keys, sizeof(keys) / sizeof(*keys))) goto fail;
    for (unsigned field = 0; field < 2; ++field) {
        *value = JS_GetPropertyStr(ctx, *options, keys[field]);
        if (JS_IsException(*value)) goto fail;
        if (!JS_IsString(ctx, *value)) goto invalid;
        JSCStringBuf buffer;
        size_t length;
        const char *text = JS_ToCStringLen(ctx, &length, *value, &buffer);
        if (text == NULL) goto fail;
        bool found = false;
        if (field == 0) {
            for (unsigned i = 0; i < sizeof(modes) / sizeof(*modes); ++i) {
                const char *name = esp32_mquickjs_wifi_tx_phy_name(modes[i]);
                if (name != NULL && length == strlen(name) && memcmp(text, name, length) == 0) {
                    config->phymode = modes[i]; found = true; break;
                }
            }
        } else {
            size_t count;
            const esp32_mquickjs_wifi_tx_rate_entry_t *entries = esp32_mquickjs_wifi_tx_rate_entries(&count);
            for (size_t i = 0; i < count; ++i) {
                if (length == strlen(entries[i].name) && memcmp(text, entries[i].name, length) == 0) {
                    config->rate = entries[i].rate; found = true; break;
                }
            }
        }
        if (!found) goto invalid;
    }
    for (unsigned field = 2; field < 4; ++field) {
        *value = JS_GetPropertyStr(ctx, *options, keys[field]);
        if (JS_IsException(*value)) goto fail;
        if (JS_IsUndefined(*value)) continue;
        if (!JS_IsBool(*value)) goto invalid;
        if (field == 2) config->ersu = *value == JS_TRUE;
        else config->dcm = *value == JS_TRUE;
    }
    if (!esp32_mquickjs_wifi_tx_rate_valid(config)) goto invalid;
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return true;
invalid:
    JS_ThrowTypeError(ctx, "Invalid or target-unsupported Wi-Fi PHY/rate combination or boolean option");
fail:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return false;
}

static JSValue tx_rate_status_to_js(JSContext *ctx, wifi_interface_t interface,
    const esp32_mquickjs_wifi_tx_rate_record_t *record, uint32_t generation,
    const esp32_mquickjs_wifi_tx_rate_lease_t *temporary)
{
    JSGCRef result_ref, config_ref, temporary_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *config = JS_PushGCRef(ctx, &config_ref);
    JSValue *lease = JS_PushGCRef(ctx, &temporary_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    bool known = record->known && record->generation == generation;
#define SET(object, name, expression) do { \
    if (!esp32_mquickjs_set_property_ref(ctx, object, name, expression)) goto fail; \
} while (0)
    SET(result, "interface", JS_NewString(ctx, interface == WIFI_IF_STA ? "station" : "access-point"));
    SET(result, "known", JS_NewBool(known));
    SET(result, "uncertain", JS_NewBool(record->uncertain));
    SET(result, "source", known ? JS_NewString(ctx, "framework-write") : JS_NULL);
    SET(result, "radioGeneration", JS_NewUint32(ctx, generation));
    SET(result, "writeGeneration", JS_NewUint32(ctx, record->generation));
    SET(result, "writeIdentity", JS_NewUint32(ctx, record->write_identity));
    SET(result, "error", record->error != ESP_OK ? JS_NewInt32(ctx, record->error) : JS_NULL);
    SET(result, "rollbackError", record->rollback_error != ESP_OK ? JS_NewInt32(ctx, record->rollback_error) : JS_NULL);
    *config = JS_NULL;
    if (known) {
        *config = JS_NewObject(ctx);
        if (JS_IsException(*config)) goto fail;
        SET(config, "phy", JS_NewString(ctx, esp32_mquickjs_wifi_tx_phy_name(record->config.phymode)));
        SET(config, "rate", JS_NewString(ctx, esp32_mquickjs_wifi_tx_rate_name(record->config.rate)));
        SET(config, "ersu", JS_NewBool(record->config.ersu));
        SET(config, "dcm", JS_NewBool(record->config.dcm));
    }
    SET(result, "config", *config);
    *lease = JS_NULL;
    if (temporary != NULL && temporary->identity != 0U) {
        *lease = JS_NewObject(ctx);
        if (JS_IsException(*lease)) goto fail;
        SET(lease, "radioGeneration", JS_NewUint32(ctx, temporary->generation));
        SET(lease, "radioLeaseIdentity", JS_NewUint32(ctx, temporary->identity));
        SET(lease, "writeIdentity", JS_NewUint32(ctx, temporary->write_identity));
        SET(lease, "restorePending", JS_NewBool(temporary->restore_pending));
        SET(lease, "restoreError", temporary->restore_error != ESP_OK ? JS_NewInt32(ctx, temporary->restore_error) : JS_NULL);
        *config = JS_NewObject(ctx);
        if (JS_IsException(*config)) goto fail;
        SET(config, "phy", JS_NewString(ctx, esp32_mquickjs_wifi_tx_phy_name(temporary->previous.phymode)));
        SET(config, "rate", JS_NewString(ctx, esp32_mquickjs_wifi_tx_rate_name(temporary->previous.rate)));
        SET(config, "ersu", JS_NewBool(temporary->previous.ersu));
        SET(config, "dcm", JS_NewBool(temporary->previous.dcm));
        SET(lease, "previous", *config);
    }
    SET(result, "temporaryLease", *lease);
#undef SET
    JS_PopGCRef(ctx, &temporary_ref);
    JS_PopGCRef(ctx, &config_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &temporary_ref);
    JS_PopGCRef(ctx, &config_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue tx_rate_error(JSContext *ctx, const char *operation, esp_err_t err,
    const esp32_mquickjs_wifi_tx_rate_write_t *write, wifi_interface_t interface,
    const esp32_mquickjs_wifi_tx_rate_record_t *record, uint32_t generation,
    const esp32_mquickjs_wifi_tx_rate_lease_t *temporary)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = tx_rate_status_to_js(ctx, interface, record, generation, temporary);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", JS_NewString(ctx, write->attempted ? "write" : "admission")) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "attempted", JS_NewBool(write->attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "driverAccepted", JS_NewBool(write->accepted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "rollbackAttempted", JS_NewBool(write->rollback_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "restored", JS_NewBool(write->restored))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, "WIFI_TX_RATE_FAILED", operation,
        "Wi-Fi TX rate operation failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

static JSValue driver_protocols_to_js(JSContext *ctx, uint16_t bits)
{
    static const char *const names[] = {"11b", "11g", "11n", "11a", "11ac", "11ax", "lr"};
    static const uint16_t flags[] = {WIFI_PROTOCOL_11B, WIFI_PROTOCOL_11G, WIFI_PROTOCOL_11N,
        WIFI_PROTOCOL_11A, WIFI_PROTOCOL_11AC, WIFI_PROTOCOL_11AX, WIFI_PROTOCOL_LR};
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    uint32_t count = 0;
    for (size_t i = 0; i < sizeof(flags) / sizeof(*flags); ++i) {
        if (!(bits & flags[i])) continue;
        *item = JS_NewString(ctx, names[i]);
        if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *array, count++, *item))) goto fail;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

/* Only validated native snapshots reach conversion; native reads hold no JS roots. */
static JSValue driver_phy_to_js(JSContext *ctx, esp32_mquickjs_wifi_phy_query_t query,
    const esp32_mquickjs_wifi_phy_readback_t *value)
{
    if (query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOL) return driver_protocols_to_js(ctx, value->protocols.ghz_2g);
    if (query == ESP32_MQUICKJS_WIFI_PHY_BANDWIDTH) return JS_NewInt32(ctx, value->bandwidths.ghz_2g == WIFI_BW20 ? 20 : 40);
    bool protocol = query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS;
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    for (unsigned band = 1; band <= 2; band <<= 1) {
        if (!(value->bands & band)) continue;
        const char *key = protocol ? (band == 1 ? "ghz2" : "ghz5") : (band == 1 ? "ghz2MHz" : "ghz5MHz");
        uint16_t bits = band == 1 ? value->protocols.ghz_2g : value->protocols.ghz_5g;
        wifi_bandwidth_t width = band == 1 ? value->bandwidths.ghz_2g : value->bandwidths.ghz_5g;
        if (!esp32_mquickjs_set_property_ref(ctx, result, key, protocol ? driver_protocols_to_js(ctx, bits)
            : JS_NewInt32(ctx, width == WIFI_BW20 ? 20 : 40))) goto fail;
    }
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue driver_read_error(JSContext *ctx, const char *interface,
    const char *operation, esp_err_t err, const char *stage)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", JS_NewString(ctx, stage)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "interface", interface != NULL ? JS_NewString(ctx, interface) : JS_NULL)) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, "WIFI_DRIVER_READ_FAILED", operation,
        "Wi-Fi driver read failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

static JSValue driver_antenna_to_js(JSContext *ctx, bool gpio,
    const esp32_mquickjs_wifi_antenna_snapshot_t *snapshot)
{
    JSGCRef root_ref, array_ref, item_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref);
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *root = JS_NewObject(ctx);
    if (JS_IsException(*root)) goto fail;
    if (gpio) {
        *array = JS_NewArray(ctx, 4);
        if (JS_IsException(*array)) goto fail;
        for (unsigned i = 0; i < 4; ++i) {
            *item = JS_NewObject(ctx);
            if (JS_IsException(*item) ||
                !esp32_mquickjs_set_property_ref(ctx, item, "selected", JS_NewBool(snapshot->gpio.gpio_cfg[i].gpio_select)) ||
                !esp32_mquickjs_set_property_ref(ctx, item, "gpio", JS_NewUint32(ctx, snapshot->gpio.gpio_cfg[i].gpio_num)) ||
                JS_IsException(JS_SetPropertyUint32(ctx, *array, i, *item))) goto fail;
        }
        if (!esp32_mquickjs_set_property_ref(ctx, root, "gpios", *array)) goto fail;
    } else {
        const esp_phy_ant_config_t *config = &snapshot->config;
        if (!esp32_mquickjs_set_property_ref(ctx, root, "rxMode", JS_NewString(ctx,
                config->rx_ant_mode == ESP_PHY_ANT_MODE_ANT0 ? "ant0" : config->rx_ant_mode == ESP_PHY_ANT_MODE_ANT1 ? "ant1" : "auto")) ||
            !esp32_mquickjs_set_property_ref(ctx, root, "txMode", JS_NewString(ctx,
                config->tx_ant_mode == ESP_PHY_ANT_MODE_ANT0 ? "ant0" : config->tx_ant_mode == ESP_PHY_ANT_MODE_ANT1 ? "ant1" : "auto")) ||
            !esp32_mquickjs_set_property_ref(ctx, root, "rxDefault", JS_NewString(ctx,
                config->rx_ant_default == ESP_PHY_ANT_ANT0 ? "ant0" : "ant1")) ||
            !esp32_mquickjs_set_property_ref(ctx, root, "enabledAnt0", JS_NewUint32(ctx, config->enabled_ant0)) ||
            !esp32_mquickjs_set_property_ref(ctx, root, "enabledAnt1", JS_NewUint32(ctx, config->enabled_ant1))) goto fail;
    }
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_PopGCRef(ctx, &root_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &root_ref);
    return JS_EXCEPTION;
}

static JSValue driver_read_antenna(JSContext *ctx, int argc, bool gpio, const char *operation)
{
    if (argc != 0) return JS_ThrowTypeError(ctx, "%s expects no arguments", operation);
    esp32_mquickjs_wifi_antenna_snapshot_t snapshot;
    const char *stage;
    esp_err_t err = esp32_mquickjs_wifi_radio_read_antenna(gpio, &snapshot, &stage);
    if (err != ESP_OK) return driver_read_error(ctx, NULL, operation, err, stage);
    return driver_antenna_to_js(ctx, gpio, &snapshot);
}

JSValue js_wifi_driver_get_antenna(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    return driver_read_antenna(ctx, argc, false, "wifi.driver.getAntenna");
}

JSValue js_wifi_driver_get_antenna_gpio(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    return driver_read_antenna(ctx, argc, true, "wifi.driver.getAntennaGpio");
}

static JSValue driver_state_to_js(JSContext *ctx, esp32_mquickjs_wifi_driver_state_query_t query,
    const esp32_mquickjs_wifi_driver_state_readback_t *value)
{
    if (query == ESP32_MQUICKJS_WIFI_DRIVER_STATE_MODE) {
        const char *mode = value->mode == WIFI_MODE_NULL ? "off" : value->mode == WIFI_MODE_STA ? "station" :
            value->mode == WIFI_MODE_AP ? "softAP" : "station+softAP";
        return JS_NewString(ctx, mode);
    }
    if (query == ESP32_MQUICKJS_WIFI_DRIVER_STATE_COUNTRY)
        return esp32_mquickjs_wifi_country_to_js(ctx, &value->country);
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    const char *secondary = value->secondary == WIFI_SECOND_CHAN_NONE ? "none" :
        value->secondary == WIFI_SECOND_CHAN_ABOVE ? "above" : "below";
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channel", JS_NewUint32(ctx, value->channel)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "secondaryChannel", JS_NewString(ctx, secondary)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "band", JS_NewString(ctx, value->channel <= 14 ? "2.4GHz" : "5GHz")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channelGeneration",
            query == ESP32_MQUICKJS_WIFI_DRIVER_STATE_HOME_CHANNEL ? JS_NULL : JS_NewUint32(ctx, value->channel_generation))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue driver_read_state(JSContext *ctx, int argc,
    esp32_mquickjs_wifi_driver_state_query_t query, const char *operation)
{
    if (argc != 0) return JS_ThrowTypeError(ctx, "%s expects no arguments", operation);
    esp32_mquickjs_wifi_driver_state_readback_t value;
    const char *stage;
    esp_err_t err = esp32_mquickjs_wifi_radio_read_state(query, &value, &stage);
    if (err != ESP_OK) return driver_read_error(ctx, NULL, operation, err, stage);
    return driver_state_to_js(ctx, query, &value);
}

JSValue js_wifi_driver_get_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argv;
    return driver_read_state(ctx, argc, ESP32_MQUICKJS_WIFI_DRIVER_STATE_MODE, "wifi.driver.getMode");
}

JSValue js_wifi_driver_get_country(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argv;
    return driver_read_state(ctx, argc, ESP32_MQUICKJS_WIFI_DRIVER_STATE_COUNTRY, "wifi.driver.getCountry");
}

JSValue js_wifi_driver_get_channel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argv;
    return driver_read_state(ctx, argc, ESP32_MQUICKJS_WIFI_DRIVER_STATE_CHANNEL, "wifi.driver.getChannel");
}

JSValue js_wifi_driver_get_home_channel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argv;
    return driver_read_state(ctx, argc, ESP32_MQUICKJS_WIFI_DRIVER_STATE_HOME_CHANNEL, "wifi.driver.getHomeChannel");
}

static JSValue driver_read_phy(JSContext *ctx, int argc, JSValue *argv,
    esp32_mquickjs_wifi_phy_query_t query, const char *operation)
{
    if (argc != 1) return JS_ThrowTypeError(ctx, "%s(interface) expects one argument", operation);
    wifi_interface_t interface;
    if (!tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_phy_readback_t value;
    const char *stage;
    esp_err_t err = esp32_mquickjs_wifi_radio_read_phy(interface, query, &value, &stage);
    if (err == ESP_OK) return driver_phy_to_js(ctx, query, &value);
    return driver_read_error(ctx, interface == WIFI_IF_STA ? "station" : "access-point", operation, err, stage);
}

static JSValue driver_scalar_to_js(JSContext *ctx, esp32_mquickjs_wifi_driver_query_t query, int64_t value)
{
    /* Native boundary validated enums, units and exact integer range. */
    switch (query) {
    case ESP32_MQUICKJS_WIFI_DRIVER_BAND:
        return JS_NewString(ctx, value == WIFI_BAND_2G ? "2.4GHz" : "5GHz");
    case ESP32_MQUICKJS_WIFI_DRIVER_BAND_MODE:
        return JS_NewString(ctx, value == WIFI_BAND_MODE_2G_ONLY ? "2.4GHz-only" :
            value == WIFI_BAND_MODE_5G_ONLY ? "5GHz-only" : "auto");
    case ESP32_MQUICKJS_WIFI_DRIVER_POWER_SAVE:
        return JS_NewString(ctx, value == WIFI_PS_NONE ? "none" : value == WIFI_PS_MIN_MODEM ? "minimum" : "maximum");
    case ESP32_MQUICKJS_WIFI_DRIVER_NEGOTIATED_PHY:
        return JS_NewString(ctx, esp32_mquickjs_wifi_tx_phy_name((wifi_phy_mode_t)value));
    case ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER:
        return JS_NewFloat64(ctx, (double)value / 4.0);
    default:
        return JS_NewInt64(ctx, value);
    }
}

static JSValue driver_read_scalar(JSContext *ctx, int argc, JSValue *argv,
    esp32_mquickjs_wifi_driver_query_t query, const char *operation)
{
    bool by_interface = query == ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME || query == ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME;
    if (argc != (by_interface ? 1 : 0)) return JS_ThrowTypeError(ctx, "%s expects %d arguments", operation, by_interface ? 1 : 0);
    wifi_interface_t interface = WIFI_IF_STA;
    if (by_interface && !tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    int64_t value;
    const char *stage;
    esp_err_t err = esp32_mquickjs_wifi_radio_read_driver(query, interface, &value, &stage);
    if (err == ESP_OK) return driver_scalar_to_js(ctx, query, value);
    bool station = query == ESP32_MQUICKJS_WIFI_DRIVER_RSSI || query == ESP32_MQUICKJS_WIFI_DRIVER_AID ||
                   query == ESP32_MQUICKJS_WIFI_DRIVER_NEGOTIATED_PHY;
    const char *name = by_interface ? (interface == WIFI_IF_STA ? "station" : "access-point") : station ? "station" : NULL;
    return driver_read_error(ctx, name, operation, err, stage);
}

JSValue js_wifi_driver_get_band(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_BAND, "wifi.driver.getBand");
}

JSValue js_wifi_driver_get_band_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_BAND_MODE, "wifi.driver.getBandMode");
}

JSValue js_wifi_driver_get_power_save(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_POWER_SAVE, "wifi.driver.getPowerSave");
}

JSValue js_wifi_driver_get_tx_power(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER, "wifi.driver.getTxPower");
}

JSValue js_wifi_driver_get_rssi(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_RSSI, "wifi.driver.getRssi");
}

JSValue js_wifi_driver_get_aid(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_AID, "wifi.driver.getAid");
}

JSValue js_wifi_driver_get_negotiated_phy(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_NEGOTIATED_PHY, "wifi.driver.getNegotiatedPhy");
}

JSValue js_wifi_driver_get_tsf_time(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME, "wifi.driver.getTsfTime");
}

JSValue js_wifi_driver_get_inactive_time(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_scalar(ctx, argc, argv, ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME, "wifi.driver.getInactiveTime");
}


JSValue js_wifi_driver_get_protocol(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_phy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_PHY_PROTOCOL, "wifi.driver.getProtocol");
}

JSValue js_wifi_driver_get_protocols(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_phy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS, "wifi.driver.getProtocols");
}

JSValue js_wifi_driver_get_bandwidth(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_phy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_PHY_BANDWIDTH, "wifi.driver.getBandwidth");
}

JSValue js_wifi_driver_get_bandwidths(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_read_phy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS, "wifi.driver.getBandwidths");
}

static bool driver_phy_capture(JSContext *ctx, JSValue input,
    esp32_mquickjs_wifi_phy_query_t query, esp32_mquickjs_wifi_phy_readback_t *output)
{
    bool protocol = query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOL || query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS;
    bool plural = query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS || query == ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS;
    static const char *const protocol_keys[] = {"ghz2", "ghz5"};
    static const char *const width_keys[] = {"ghz2MHz", "ghz5MHz"};
    const char *const *keys = protocol ? protocol_keys : width_keys;
    JSGCRef root_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *root = input;
    esp32_mquickjs_wifi_phy_readback_t captured = {0};
    memset(output, 0, sizeof(*output));
    if (plural && !esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.driver PHY config", keys, 2)) goto fail;
    for (unsigned band = 0; band < (plural ? 2U : 1U); ++band) {
        *value = plural ? JS_GetPropertyStr(ctx, *root, keys[band]) : *root;
        if (JS_IsException(*value)) goto fail;
        if (plural && JS_IsUndefined(*value)) continue;
        if (protocol) {
            uint16_t bitmap;
            if (!esp32_mquickjs_wifi_capture_protocol(ctx, *value, &bitmap)) goto fail;
            if (band == 0) captured.protocols.ghz_2g = bitmap;
            else captured.protocols.ghz_5g = bitmap;
        } else {
            uint32_t mhz;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 20, 40, &mhz) || (mhz != 20 && mhz != 40)) goto fail;
            wifi_bandwidth_t width = mhz == 20 ? WIFI_BW20 : WIFI_BW40;
            if (band == 0) captured.bandwidths.ghz_2g = width;
            else captured.bandwidths.ghz_5g = width;
        }
        captured.bands |= 1U << band;
    }
    if (captured.bands == 0U) goto fail;
    *output = captured;
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &root_ref);
    return true;
fail:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Wi-Fi PHY config requires nonempty unique protocol names or 20/40 MHz");
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &root_ref);
    return false;
}

static JSValue driver_phy_write_error(JSContext *ctx, wifi_interface_t interface,
    const char *operation, const esp32_mquickjs_wifi_radio_config_result_t *result)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "interface",
            interface == WIFI_IF_STA || interface == WIFI_IF_AP ? JS_NewString(ctx,
                interface == WIFI_IF_STA ? "station" : "access-point") : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, result->error)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", JS_NewString(ctx, result->stage)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "mutationAttempted", JS_NewBool(result->mutation_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "rollbackAttempted", JS_NewBool(result->rollback_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "rollbackComplete", JS_NewBool(result->rollback_complete)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "rollbackStage", result->rollback_stage != NULL ? JS_NewString(ctx, result->rollback_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "rollbackError", JS_NewInt32(ctx, result->rollback_error)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "persistentMutationPossible", JS_NewBool(result->persistent_mutation_possible))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    JSValue error = esp32_mquickjs_throw_native_error(ctx, "WIFI_DRIVER_WRITE_FAILED", operation,
        "Wi-Fi driver configuration write failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return error;
}

static JSValue driver_config_bytes(JSContext *ctx, const uint8_t *data, size_t length)
{
    JSGCRef array_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    for (uint32_t i = 0; i < length; ++i) {
        JSValue value = JS_NewUint32(ctx, data[i]);
        if (JS_IsException(value) || JS_IsException(JS_SetPropertyUint32(ctx, *array, i, value))) goto fail;
    }
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

static JSValue driver_config_text(JSContext *ctx, const uint8_t *data, size_t length)
{
    for (size_t offset = 0; offset < length;) {
        size_t consumed;
        int codepoint = unicode_from_utf8(data + offset, length - offset, &consumed);
        if (codepoint < 0 || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return JS_NULL;
        offset += consumed;
    }
    return JS_NewStringLen(ctx, (const char *)data, length);
}

static JSValue driver_config_enum(JSContext *ctx, int id, const char *name)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "id", JS_NewInt32(ctx, id)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "name", name != NULL ? JS_NewString(ctx, name) : JS_NULL)) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static const char *driver_config_scan_name(int id)
{
    return id == WIFI_FAST_SCAN ? "fast" : id == WIFI_ALL_CHANNEL_SCAN ? "all-channel" : NULL;
}

static const char *driver_config_sort_name(int id)
{
    return id == WIFI_CONNECT_AP_BY_SIGNAL ? "signal" : id == WIFI_CONNECT_AP_BY_SECURITY ? "security" : NULL;
}

static const char *driver_config_auth_name(int id)
{
    switch (id) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-enterprise";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    case WIFI_AUTH_WAPI_PSK: return "wapi";
    case WIFI_AUTH_OWE: return "owe";
    case WIFI_AUTH_WPA3_ENT_192: return "wpa3-enterprise-192";
    case WIFI_AUTH_DPP: return "dpp";
    case WIFI_AUTH_WPA3_ENTERPRISE: return "wpa3-enterprise";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "wpa2/wpa3-enterprise";
    case WIFI_AUTH_WPA_ENTERPRISE: return "wpa-enterprise";
    case WIFI_AUTH_UNKNOWN: return "unknown";
    default: return NULL;
    }
}

static const char *driver_config_pwe_name(int id)
{
    switch (id) {
    case WPA3_SAE_PWE_UNSPECIFIED: return "unspecified";
    case WPA3_SAE_PWE_HUNT_AND_PECK: return "hunting-and-pecking";
    case WPA3_SAE_PWE_HASH_TO_ELEMENT: return "hash-to-element";
    case WPA3_SAE_PWE_BOTH: return "both";
    default: return NULL;
    }
}

static const char *driver_config_pk_name(int id)
{
    switch (id) {
    case WPA3_SAE_PK_MODE_AUTOMATIC: return "automatic";
    case WPA3_SAE_PK_MODE_ONLY: return "only";
    case WPA3_SAE_PK_MODE_DISABLED: return "disabled";
    default: return NULL;
    }
}

static const char *driver_config_cipher_name(int id)
{
    switch (id) {
    case WIFI_CIPHER_TYPE_NONE: return "none";
    case WIFI_CIPHER_TYPE_WEP40: return "wep40";
    case WIFI_CIPHER_TYPE_WEP104: return "wep104";
    case WIFI_CIPHER_TYPE_TKIP: return "tkip";
    case WIFI_CIPHER_TYPE_CCMP: return "ccmp";
    case WIFI_CIPHER_TYPE_TKIP_CCMP: return "tkip/ccmp";
    case WIFI_CIPHER_TYPE_AES_CMAC128: return "aes-cmac128";
    case WIFI_CIPHER_TYPE_SMS4: return "sms4";
    case WIFI_CIPHER_TYPE_GCMP: return "gcmp";
    case WIFI_CIPHER_TYPE_GCMP256: return "gcmp-256";
    case WIFI_CIPHER_TYPE_AES_GMAC128: return "aes-gmac128";
    case WIFI_CIPHER_TYPE_AES_GMAC256: return "aes-gmac256";
    default: return NULL;
    }
}

static JSValue driver_interface_config_to_js(JSContext *ctx, wifi_interface_t interface,
    const wifi_config_t *config, bool include_secrets, const char *operation)
{
#if !CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK
    include_secrets = false;
#endif
    bool station = interface == WIFI_IF_STA;
    const uint8_t *ssid = station ? config->sta.ssid : config->ap.ssid;
    const uint8_t *password = station ? config->sta.password : config->ap.password;
    size_t ssid_length = station || config->ap.ssid_len == 0
        ? strnlen((const char *)ssid, 32) : config->ap.ssid_len;
    if (ssid_length > 32 || (!station && config->ap.ssid_hidden > 1))
        return driver_read_error(ctx, station ? "station" : "access-point", operation,
            ESP_ERR_INVALID_RESPONSE, "decode");
    const wifi_pmf_config_t *pmf = station ? &config->sta.pmf_cfg : &config->ap.pmf_cfg;
    const char *pmf_name = !pmf->capable ? (pmf->required ? NULL : "disabled") : pmf->required ? "required" : "optional";
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
#define SET(name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, result, name, value)) goto fail; } while (0)
    SET("interface", JS_NewString(ctx, station ? "station" : "access-point"));
    SET("secretsIncluded", JS_NewBool(include_secrets));
    SET("ssid", driver_config_text(ctx, ssid, ssid_length));
    SET("password", include_secrets ? driver_config_text(ctx, password,
        strnlen((const char *)password, sizeof(config->sta.password))) : JS_NULL);
    SET("pmf", pmf_name != NULL ? JS_NewString(ctx, pmf_name) : JS_NULL);
#define NUMBER(name, field) SET(name, JS_NewInt32(ctx, c->field))
#define BOOLEAN(name, field) SET(name, JS_NewBool(c->field != 0))
#define ENUM(name, field, kind) SET(name, driver_config_enum(ctx, c->field, driver_config_##kind##_name(c->field)))
#define BYTES(name, field) SET(name, driver_config_bytes(ctx, (const uint8_t *)c->field, \
    strcmp(name, "ssidBytes") == 0 ? ssid_length : sizeof(c->field)))
#define SECRET(name, field) SET(name, include_secrets ? driver_config_bytes(ctx, (const uint8_t *)c->field, \
    strnlen((const char *)c->field, sizeof(c->field))) : JS_NULL)
    if (station) {
        const wifi_sta_config_t *c = &config->sta;
        WIFI_DRIVER_OBSERVE_STA(NUMBER, BOOLEAN, ENUM, BYTES, SECRET)
        char bssid[18];
        esp32_mquickjs_wireless_format_address(c->bssid, bssid);
        SET("bssid", JS_NewString(ctx, bssid));
        SET("saeH2eIdentifier", include_secrets ? driver_config_text(ctx, (const uint8_t *)c->sae_h2e_identifier,
            strnlen((const char *)c->sae_h2e_identifier, sizeof(c->sae_h2e_identifier))) : JS_NULL);
    } else {
        const wifi_ap_config_t *c = &config->ap;
        WIFI_DRIVER_OBSERVE_AP(NUMBER, BOOLEAN, ENUM, BYTES, SECRET)
    }
#undef SECRET
#undef BYTES
#undef ENUM
#undef BOOLEAN
#undef NUMBER
#undef SET
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static void driver_config_free(wifi_config_t *config)
{
    if (config == NULL) return;
    esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    esp32_mquickjs_memory_payload_free(config);
}

JSValue js_wifi_driver_get_interface_config(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1 || argc > 2) return JS_ThrowTypeError(ctx, "wifi.driver.getInterfaceConfig expects interface and optional options");
    wifi_interface_t interface;
    if (!tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    bool include_secrets = false;
    if (argc == 2) {
        static const char *const keys[] = {"includeSecrets"};
        JSGCRef options_ref, value_ref;
        JSValue *options = JS_PushGCRef(ctx, &options_ref);
        JSValue *value = JS_PushGCRef(ctx, &value_ref);
        *options = argv[1];
        bool valid = esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.driver.getInterfaceConfig", keys, 1);
        if (valid) {
            *value = JS_GetPropertyStr(ctx, *options, "includeSecrets");
            valid = JS_IsUndefined(*value) || JS_IsBool(*value);
            include_secrets = *value == JS_TRUE;
        }
        JS_PopGCRef(ctx, &value_ref);
        JS_PopGCRef(ctx, &options_ref);
        if (!valid) return JS_HasException(ctx) ? JS_EXCEPTION : JS_ThrowTypeError(ctx, "includeSecrets must be boolean");
    }
#if !CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK
    if (include_secrets) return driver_read_error(ctx, interface == WIFI_IF_STA ? "station" : "access-point",
        "wifi.driver.getInterfaceConfig", ESP_ERR_NOT_ALLOWED, "secret-readback");
#endif
    wifi_config_t *config = esp32_mquickjs_memory_wireless_calloc("wifi.driver", 1, sizeof(*config),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (config == NULL) return JS_ThrowOutOfMemory(ctx);
    const char *stage;
    esp_err_t err = esp32_mquickjs_wifi_radio_read_interface_config(interface, include_secrets, config, &stage);
    if (err != ESP_OK) {
        driver_config_free(config);
        return driver_read_error(ctx, interface == WIFI_IF_STA ? "station" : "access-point",
            "wifi.driver.getInterfaceConfig", err, stage);
    }
    JSValue result = driver_interface_config_to_js(ctx, interface, config, include_secrets, "wifi.driver.getInterfaceConfig");
    driver_config_free(config);
    return result;
}

JSValue js_wifi_driver_set_interface_config(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 2) return JS_ThrowTypeError(ctx, "wifi.driver.setInterfaceConfig expects interface and complete config");
    wifi_interface_t interface;
    if (!tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    wifi_config_t *config = esp32_mquickjs_memory_wireless_calloc("wifi.driver", 1, sizeof(*config),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (config == NULL) return JS_ThrowOutOfMemory(ctx);
    if (!esp32_mquickjs_wifi_parse_driver_config_for_operation(ctx, argv[1], interface, config, "wifi.driver.setInterfaceConfig")) {
        driver_config_free(config);
        return JS_EXCEPTION;
    }
    esp32_mquickjs_wifi_radio_config_result_t native;
    esp_err_t err = esp32_mquickjs_wifi_radio_write_interface_config(interface, config, &native);
    if (err != ESP_OK) {
        driver_config_free(config);
        return driver_phy_write_error(ctx, interface, "wifi.driver.setInterfaceConfig", &native);
    }
    JSValue result = driver_interface_config_to_js(ctx, interface, config, false, "wifi.driver.setInterfaceConfig");
    driver_config_free(config);
    return result;
}

JSValue js_wifi_driver_set_country_details(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.driver.setCountryDetails expects one complete configuration");
    wifi_country_t requested, actual;
    if (!esp32_mquickjs_wifi_capture_country_details(ctx, argv[0], &requested)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_set_country_details(&requested, &actual, &result);
    if (err != ESP_OK) return driver_phy_write_error(ctx, WIFI_IF_MAX, "wifi.driver.setCountryDetails", &result);
    /* max_tx_power is read-only, so the returned snapshot must be constructed
     * from actual native readback. A delivery OOM does not repeat the write. */
    return esp32_mquickjs_wifi_country_to_js(ctx, &actual);
}

static bool driver_antenna_capture(JSContext *ctx, JSValue input, bool gpio,
    esp32_mquickjs_wifi_antenna_snapshot_t *snapshot)
{
    static const char *const config_keys[] = {"rxMode", "txMode", "rxDefault", "enabledAnt0", "enabledAnt1"};
    static const char *const gpio_keys[] = {"gpios"};
    static const char *const pin_keys[] = {"selected", "gpio"};
    static const char *const modes[] = {"ant0", "ant1", "auto"};
    static const esp_phy_ant_mode_t native_modes[] = {ESP_PHY_ANT_MODE_ANT0, ESP_PHY_ANT_MODE_ANT1, ESP_PHY_ANT_MODE_AUTO};
    JSGCRef input_ref, array_ref, item_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &input_ref);
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *options = input;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "Wi-Fi antenna config",
        gpio ? gpio_keys : config_keys, gpio ? 1 : 5)) goto fail;
    if (gpio) {
        *array = JS_GetPropertyStr(ctx, *options, "gpios");
        if (JS_IsException(*array)) goto fail;
        if (!JS_IsArray(ctx, *array)) goto invalid;
        *value = JS_GetPropertyStr(ctx, *array, "length");
        if (JS_IsException(*value)) goto fail;
        uint32_t length;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 4, 4, &length)) goto invalid;
        for (unsigned i = 0; i < 4; ++i) {
            *item = JS_GetPropertyUint32(ctx, *array, i);
            if (JS_IsException(*item)) goto fail;
            if (!esp32_mquickjs_validate_plain_options(ctx, *item, "Wi-Fi antenna GPIO", pin_keys, 2)) goto fail;
            *value = JS_GetPropertyStr(ctx, *item, "selected");
            if (JS_IsException(*value)) goto fail;
            if (!JS_IsBool(*value)) goto invalid;
            snapshot->gpio.gpio_cfg[i].gpio_select = *value == JS_TRUE;
            *value = JS_GetPropertyStr(ctx, *item, "gpio");
            if (JS_IsException(*value)) goto fail;
            uint32_t pin;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 127, &pin)) goto invalid;
            snapshot->gpio.gpio_cfg[i].gpio_num = pin;
        }
    } else {
        for (unsigned i = 0; i < 5; ++i) {
            *value = JS_GetPropertyStr(ctx, *options, config_keys[i]);
            if (JS_IsException(*value)) goto fail;
            if (i < 3) {
                size_t mode;
                if (!esp32_mquickjs_value_to_enum(ctx, *value, modes, i == 2 ? 2 : 3, &mode)) goto invalid;
                if (i == 0) snapshot->config.rx_ant_mode = native_modes[mode];
                else if (i == 1) snapshot->config.tx_ant_mode = native_modes[mode];
                else snapshot->config.rx_ant_default = mode == 0 ? ESP_PHY_ANT_ANT0 : ESP_PHY_ANT_ANT1;
            } else {
                uint32_t selector;
                if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 15, &selector)) goto invalid;
                if (i == 3) snapshot->config.enabled_ant0 = selector;
                else snapshot->config.enabled_ant1 = selector;
            }
        }
        if (!esp32_mquickjs_wifi_antenna_valid(&snapshot->config)) goto invalid;
    }
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &input_ref);
    return true;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Invalid or incomplete Wi-Fi antenna configuration");
fail:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &input_ref);
    return false;
}

static JSValue driver_write_antenna(JSContext *ctx, int argc, JSValue *argv, bool gpio, const char *operation)
{
    if (argc != 1) return JS_ThrowTypeError(ctx, "%s expects one complete configuration", operation);
    esp32_mquickjs_wifi_antenna_snapshot_t snapshot;
    if (!driver_antenna_capture(ctx, argv[0], gpio, &snapshot)) return JS_EXCEPTION;
    /* Finish all user getters and success-result allocations before mutation.
     * The native transaction performs semantic readback against this capture. */
    JSGCRef output_ref;
    JSValue *output = JS_PushGCRef(ctx, &output_ref);
    *output = driver_antenna_to_js(ctx, gpio, &snapshot);
    if (JS_IsException(*output)) { JS_PopGCRef(ctx, &output_ref); return JS_EXCEPTION; }
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t error = esp32_mquickjs_wifi_radio_write_antenna(gpio, &snapshot, &result);
    if (error != ESP_OK) {
        JS_PopGCRef(ctx, &output_ref);
        return driver_phy_write_error(ctx, WIFI_IF_MAX, operation, &result);
    }
    return JS_PopGCRef(ctx, &output_ref);
}

JSValue js_wifi_driver_set_antenna(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_antenna(ctx, argc, argv, false, "wifi.driver.setAntenna");
}

JSValue js_wifi_driver_set_antenna_gpio(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_antenna(ctx, argc, argv, true, "wifi.driver.setAntennaGpio");
}

JSValue esp32_mquickjs_wifi_policies_to_js(JSContext *ctx)
{
    static const char *const names[ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT] = {
        "dynamicCarrierSense", "station11bDisabled", "accessPoint11bDisabled", "coexistencePowerManagement",
#if CONFIG_SOC_WIFI_HE_SUPPORT
        "bssColorCollisionReporting",
#endif
    };
    esp32_mquickjs_wifi_policy_state_t state;
    esp32_mquickjs_wifi_radio_policy_status(&state);
    JSGCRef root_ref, entry_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref);
    JSValue *entry = JS_PushGCRef(ctx, &entry_ref);
    *root = JS_NewObject(ctx);
    if (JS_IsException(*root) ||
        !esp32_mquickjs_set_property_ref(ctx, root, "revision", JS_NewUint32(ctx, state.revision)) ||
        !esp32_mquickjs_set_property_ref(ctx, root, "identityExhausted", JS_NewBool(state.revision == UINT32_MAX))) goto fail;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT; ++i) {
        const esp32_mquickjs_wifi_policy_record_t *record = &state.records[i];
        *entry = JS_NewObject(ctx);
        if (JS_IsException(*entry) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "generation", JS_NewUint32(ctx, record->generation)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "revision", JS_NewUint32(ctx, record->revision)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "acceptedRevision", JS_NewUint32(ctx, record->accepted_revision)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "configured", JS_NewBool(record->configured)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "known", JS_NewBool(record->known)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "uncertain", JS_NewBool(record->uncertain)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "requested", record->revision != 0U ? JS_NewBool(record->requested) : JS_NULL) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "value", record->known ? JS_NewBool(record->value) : JS_NULL) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "lastAcceptedValue", record->configured ? JS_NewBool(record->value) : JS_NULL) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "error", JS_NewInt32(ctx, record->error)) ||
            !esp32_mquickjs_set_property_ref(ctx, root, names[i], *entry)) goto fail;
    }
    JSValue result = *root;
    JS_PopGCRef(ctx, &entry_ref);
    JS_PopGCRef(ctx, &root_ref);
    return result;
fail:
    JS_PopGCRef(ctx, &entry_ref);
    JS_PopGCRef(ctx, &root_ref);
    return JS_EXCEPTION;
}

JSValue esp32_mquickjs_wifi_rssi_request_to_js(JSContext *ctx)
{
    esp32_mquickjs_wifi_rssi_request_t state;
    bool generation_active = esp32_mquickjs_wifi_radio_rssi_request_status(&state);
    bool attempted = state.revision != 0U;
    JSGCRef ref;
    JSValue *value = JS_PushGCRef(ctx, &ref);
    *value = JS_NewObject(ctx);
    if (JS_IsException(*value) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "revision", JS_NewUint32(ctx, state.revision)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "identityExhausted", JS_NewBool(state.revision == UINT32_MAX)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "generation", attempted ? JS_NewUint32(ctx, state.generation) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "generationActive", JS_NewBool(generation_active)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "requestedDbm", attempted ? JS_NewInt32(ctx, state.requested_dbm) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "accepted", JS_NewBool(attempted && state.error == ESP_OK)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "espCode", attempted ? JS_NewInt32(ctx, state.error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "espName", attempted ? JS_NewString(ctx, esp_err_to_name(state.error)) : JS_NULL)) {
        JS_PopGCRef(ctx, &ref);
        return JS_EXCEPTION;
    }
    JSValue result = *value;
    JS_PopGCRef(ctx, &ref);
    return result;
}

JSValue esp32_mquickjs_wifi_interval_to_js(JSContext *ctx)
{
    esp32_mquickjs_wifi_interval_state_t state;
    esp32_mquickjs_wifi_radio_interval_status(&state);
    JSGCRef ref;
    JSValue *value = JS_PushGCRef(ctx, &ref);
    *value = JS_NewObject(ctx);
    if (JS_IsException(*value) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "generation", JS_NewUint32(ctx, state.generation)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "revision", JS_NewUint32(ctx, state.revision)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "known", JS_NewBool(state.known)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "uncertain", JS_NewBool(state.uncertain)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "milliseconds", state.known ? JS_NewInt32(ctx, state.value) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "previousMilliseconds", state.owner.identity != 0U ? JS_NewInt32(ctx, state.previous) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "ownerIdentity", JS_NewUint32(ctx, state.owner.owner_identity)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "tokenIdentity", JS_NewUint32(ctx, state.owner.identity)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "restorePending", JS_NewBool(state.restore_pending)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "error", JS_NewInt32(ctx, state.error)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "restoreError", JS_NewInt32(ctx, state.restore_error))) {
        JS_PopGCRef(ctx, &ref);
        return JS_EXCEPTION;
    }
    JSValue result = *value;
    JS_PopGCRef(ctx, &ref);
    return result;
}

JSValue js_wifi_driver_set_connectionless_wake_interval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    uint32_t milliseconds;
    if (argc != 1 || !esp32_mquickjs_value_to_bounded_u32(ctx, argv[0], 0, UINT16_MAX, &milliseconds)) {
        if (JS_HasException(ctx)) return JS_EXCEPTION;
        return JS_ThrowTypeError(ctx, "wifi.driver.setConnectionlessWakeInterval expects integer milliseconds 0..65535 (0 selects default mode)");
    }
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_apply_interval((uint16_t)milliseconds, &result);
    if (err != ESP_OK) return driver_phy_write_error(ctx, WIFI_IF_MAX, "wifi.driver.setConnectionlessWakeInterval", &result);
    return JS_NewUint32(ctx, milliseconds);
}

static JSValue driver_write_policy(JSContext *ctx, int argc, JSValue *argv,
    esp32_mquickjs_wifi_policy_control_t control, const char *operation)
{
    bool by_interface = control == ESP32_MQUICKJS_WIFI_POLICY_11B_RATE;
    if (argc != (by_interface ? 2 : 1)) return JS_ThrowTypeError(ctx, "%s has invalid argument count", operation);
    wifi_interface_t interface = WIFI_IF_MAX;
    if (by_interface && !tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    JSValue value = argv[by_interface ? 1 : 0];
    if (!JS_IsBool(value)) return JS_ThrowTypeError(ctx, "%s expects a boolean", operation);
    bool accepted;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_apply_policy(control, interface, value == JS_TRUE, &accepted, &result);
    if (err != ESP_OK) return driver_phy_write_error(ctx, interface, operation, &result);
    return JS_NewBool(accepted);
}

JSValue js_wifi_driver_set_dynamic_carrier_sense(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_policy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_POLICY_DYNAMIC_CS, "wifi.driver.setDynamicCarrierSense");
}

JSValue js_wifi_driver_configure_11b_rate(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_policy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_POLICY_11B_RATE, "wifi.driver.configure11bRate");
}

JSValue js_wifi_driver_set_coexistence_power_management(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_policy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_POLICY_COEX_POWER, "wifi.driver.setCoexistencePowerManagement");
}

JSValue js_wifi_driver_set_bss_color_collision_reporting(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_policy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_POLICY_BSS_COLOR,
        "wifi.driver.setBssColorCollisionReporting");
}

JSValue js_wifi_driver_get_event_mask(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.driver.getEventMask expects no arguments");
    uint32_t actual;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_event_mask(false, 0, &actual, &result);
    if (err != ESP_OK) return driver_read_error(ctx, NULL, "wifi.driver.getEventMask", err, result.stage);
    return JS_NewUint32(ctx, actual);
}

JSValue js_wifi_driver_set_event_mask(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    uint32_t requested;
    if (argc != 1 || !esp32_mquickjs_value_to_bounded_u32(ctx, argv[0], 0, UINT32_MAX, &requested) ||
        (requested & ~((uint32_t)WIFI_EVENT_MASK_AP_PROBEREQRECVED)) != 0U) {
        if (JS_HasException(ctx)) return JS_EXCEPTION;
        return JS_ThrowTypeError(ctx, "wifi.driver.setEventMask expects 0 or the AP probe-request mask (1); control/unknown bits are reserved");
    }
    uint32_t actual;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_event_mask(true, requested, &actual, &result);
    if (err != ESP_OK) return driver_phy_write_error(ctx, WIFI_IF_MAX, "wifi.driver.setEventMask", &result);
    return JS_NewUint32(ctx, actual);
}

JSValue js_wifi_driver_restore(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.driver.restore() takes no arguments");
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_restore(&result);
    return err == ESP_OK ? JS_TRUE : driver_phy_write_error(ctx, WIFI_IF_MAX, "wifi.driver.restore", &result);
}

JSValue js_wifi_driver_set_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    static const char *const names[] = {"off", "station", "softAP", "station+softAP"};
    static const wifi_mode_t modes[] = {WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA};
    size_t choice;
    if (argc != 1 || !esp32_mquickjs_value_to_enum(ctx, argv[0], names, sizeof(names) / sizeof(*names), &choice)) {
        if (JS_HasException(ctx)) return JS_EXCEPTION;
        return JS_ThrowTypeError(ctx, "wifi.driver.setMode expects off, station, softAP or station+softAP");
    }
    JSGCRef input_ref;
    JSValue *input = JS_PushGCRef(ctx, &input_ref);
    *input = argv[0];
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_set_mode(modes[choice], &result);
    JSValue output = err == ESP_OK ? *input : driver_phy_write_error(ctx, WIFI_IF_MAX, "wifi.driver.setMode", &result);
    JS_PopGCRef(ctx, &input_ref);
    return output;
}

JSValue js_wifi_driver_set_storage(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1 || !JS_IsString(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "wifi.driver.setStorage expects ram or flash");
    JSCStringBuf buffer;
    size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, argv[0], &buffer);
    if (text == NULL) return JS_EXCEPTION;
    wifi_storage_t storage;
    if (length == 3 && memcmp(text, "ram", 3) == 0) storage = WIFI_STORAGE_RAM;
    else if (length == 5 && memcmp(text, "flash", 5) == 0) storage = WIFI_STORAGE_FLASH;
    else return JS_ThrowTypeError(ctx, "wifi.driver.setStorage expects ram or flash");
    /* Return the rooted validated input; no result allocation after mutation. */
    JSGCRef input_ref;
    JSValue *input = JS_PushGCRef(ctx, &input_ref);
    *input = argv[0];
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_set_storage(storage, &result);
    JSValue output = err == ESP_OK ? *input :
        driver_phy_write_error(ctx, WIFI_IF_MAX, "wifi.driver.setStorage", &result);
    JS_PopGCRef(ctx, &input_ref);
    return output;
}

JSValue js_wifi_driver_disable_pmf(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.driver.disablePmf(interface) expects one argument");
    wifi_interface_t interface;
    if (!tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_disable_pmf(interface, &result);
    if (err != ESP_OK) return driver_phy_write_error(ctx, interface, "wifi.driver.disablePmf", &result);
    return JS_UNDEFINED;
}

static JSValue driver_write_phy(JSContext *ctx, int argc, JSValue *argv,
    esp32_mquickjs_wifi_phy_query_t query, const char *operation)
{
    if (argc != 2) return JS_ThrowTypeError(ctx, "%s(interface, config) expects two arguments", operation);
    wifi_interface_t interface;
    if (!tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_phy_readback_t requested, actual;
    if (!driver_phy_capture(ctx, argv[1], query, &requested)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_write_phy(interface, query, &requested, &actual, &result);
    if (err != ESP_OK) return driver_phy_write_error(ctx, interface, operation, &result);
    /* Mutation is committed before conversion. Inspect getters/status after OOM. */
    return driver_phy_to_js(ctx, query, &actual);
}

JSValue js_wifi_driver_set_protocol(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_phy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_PHY_PROTOCOL, "wifi.driver.setProtocol");
}

JSValue js_wifi_driver_set_protocols(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_phy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS, "wifi.driver.setProtocols");
}

JSValue js_wifi_driver_set_bandwidth(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_phy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_PHY_BANDWIDTH, "wifi.driver.setBandwidth");
}

JSValue js_wifi_driver_set_bandwidths(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_write_phy(ctx, argc, argv, ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS, "wifi.driver.setBandwidths");
}

static bool driver_rx_statistics_capture(JSContext *ctx, JSValue input, uint8_t *selection)
{
    static const char *const keys[] = {"ordinary", "multiUser"};
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *value = JS_PushGCRef(ctx, &value_ref);
    *options = input;
    *selection = 0;
    bool success = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.driver.configureRxStatistics", keys, 2)) goto done;
    for (unsigned i = 0; i < 2; ++i) {
        *value = JS_GetPropertyStr(ctx, *options, keys[i]);
        if (JS_IsException(*value)) goto done;
        if (!JS_IsBool(*value)) {
            JS_ThrowTypeError(ctx, "RX statistics require ordinary and multiUser booleans");
            goto done;
        }
        if (*value == JS_TRUE) *selection |= 1U << i;
    }
    success = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return success;
}

static JSValue driver_rx_statistics_to_js(JSContext *ctx, const esp32_mquickjs_wifi_he_statistics_t *actual)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ordinary", JS_NewBool(actual->ordinary)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "multiUser", JS_NewBool(actual->multi_user))) {
        JS_PopGCRef(ctx, &ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &ref);
}

static const char *const driver_statistics_categories[] = {"voice", "video", "bestEffort", "background"};

JSValue js_wifi_driver_get_statistics_config(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.driver.getStatisticsConfig() expects no arguments");
    esp32_mquickjs_wifi_he_statistics_t actual;
    const char *stage;
    esp_err_t error = esp32_mquickjs_wifi_radio_read_he_statistics(&actual, &stage);
    if (error != ESP_OK) return driver_read_error(ctx, NULL, "wifi.driver.getStatisticsConfig", error, stage);
    JSGCRef result_ref, tx_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref), *tx = JS_PushGCRef(ctx, &tx_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    *tx = JS_NewObject(ctx);
    if (JS_IsException(*tx)) goto fail;
    for (unsigned i = 0; i < 4; ++i)
        if (!esp32_mquickjs_set_property_ref(ctx, tx, driver_statistics_categories[i],
            JS_NewBool((actual.tx_mask & (1U << i)) != 0))) goto fail;
    if (!esp32_mquickjs_set_property_ref(ctx, result, "tx", *tx) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "rx", driver_rx_statistics_to_js(ctx, &actual))) goto fail;
    JS_PopGCRef(ctx, &tx_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &tx_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_driver_configure_rx_statistics(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.driver.configureRxStatistics(config) expects one argument");
    uint8_t selection;
    if (!driver_rx_statistics_capture(ctx, argv[0], &selection)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_he_statistics_t actual;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t error = esp32_mquickjs_wifi_apply_he_statistics(true, selection, false, &actual, &result);
    if (error != ESP_OK) return driver_phy_write_error(ctx, WIFI_IF_MAX, "wifi.driver.configureRxStatistics", &result);
    return driver_rx_statistics_to_js(ctx, &actual);
}

JSValue js_wifi_driver_set_tx_statistics(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 2 || !JS_IsString(ctx, argv[0]) || !JS_IsBool(argv[1]))
        return JS_ThrowTypeError(ctx, "wifi.driver.setTxStatistics expects an access category and boolean");
    bool enabled = argv[1] == JS_TRUE;
    JSCStringBuf buffer;
    size_t length;
    const char *category = JS_ToCStringLen(ctx, &length, argv[0], &buffer);
    if (category == NULL) return JS_EXCEPTION;
    unsigned selection;
    for (selection = 0; selection < 4; ++selection)
        if (length == strlen(driver_statistics_categories[selection]) &&
            memcmp(category, driver_statistics_categories[selection], length) == 0) break;
    if (selection == 4) return JS_ThrowTypeError(ctx, "Access category must be voice, video, bestEffort or background");
    esp32_mquickjs_wifi_he_statistics_t actual;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t error = esp32_mquickjs_wifi_apply_he_statistics(false, (uint8_t)selection, enabled, &actual, &result);
    if (error != ESP_OK) return driver_phy_write_error(ctx, WIFI_IF_MAX, "wifi.driver.setTxStatistics", &result);
    return JS_NewBool((actual.tx_mask & (1U << selection)) != 0);
}

static bool driver_scan_parameters_capture(JSContext *ctx, JSValue input,
    wifi_scan_default_params_t *parameters)
{
    static const char *const keys[] = {"activeMinMs", "activeMaxMs", "passiveMs", "homeChannelDwellMs"};
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *value = JS_PushGCRef(ctx, &value_ref);
    *options = input;
    uint32_t fields[4] = {0};
    bool success = false;
    memset(parameters, 0, sizeof(*parameters));
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.driver.setScanParameters", keys, 4)) goto done;
    for (unsigned i = 0; i < 4; ++i) {
        *value = JS_GetPropertyStr(ctx, *options, keys[i]);
        if (JS_IsException(*value)) goto done;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, i == 3 ? 150 : 1500, &fields[i])) {
            if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Scan parameters require four integer millisecond fields");
            goto done;
        }
    }
    parameters->scan_time.active.min = fields[0];
    parameters->scan_time.active.max = fields[1];
    parameters->scan_time.passive = fields[2];
    parameters->home_chan_dwell_time = (uint8_t)fields[3];
    if (!wifi_scan_parameters_valid(parameters)) {
        JS_ThrowTypeError(ctx, "Scan minimum must not exceed effective maximum; home dwell must be 0 or 30..150 ms");
        goto done;
    }
    success = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return success;
}

static JSValue driver_scan_parameters_to_js(JSContext *ctx, const wifi_scan_default_params_t *parameters)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "activeMinMs", JS_NewUint32(ctx, parameters->scan_time.active.min)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "activeMaxMs", JS_NewUint32(ctx, parameters->scan_time.active.max)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "passiveMs", JS_NewUint32(ctx, parameters->scan_time.passive)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "homeChannelDwellMs", JS_NewUint32(ctx, parameters->home_chan_dwell_time))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_wifi_driver_get_scan_parameters(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.driver.getScanParameters() expects no arguments");
    wifi_scan_default_params_t actual;
    const char *stage;
    esp_err_t error = esp32_mquickjs_wifi_radio_read_scan_parameters(&actual, &stage);
    if (error != ESP_OK) return driver_read_error(ctx, "station", "wifi.driver.getScanParameters", error, stage);
    return driver_scan_parameters_to_js(ctx, &actual);
}

JSValue js_wifi_driver_set_scan_parameters(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.driver.setScanParameters(config|null) expects one argument");
    bool reset = JS_IsNull(argv[0]);
    wifi_scan_default_params_t requested, actual;
    if (!reset && !driver_scan_parameters_capture(ctx, argv[0], &requested)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t error = esp32_mquickjs_wifi_apply_scan_parameters(reset ? NULL : &requested, &actual, &result);
    if (error != ESP_OK) return driver_phy_write_error(ctx, WIFI_IF_STA, "wifi.driver.setScanParameters", &result);
    /* Native state is committed before JS conversion; OOM cannot undo it. */
    return driver_scan_parameters_to_js(ctx, &actual);
}

static JSValue driver_connection_control(JSContext *ctx, int argc, JSValue *argv,
    esp32_mquickjs_wifi_connection_control_t control, const char *operation)
{
    bool observer = control == ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD;
    if (argc != (observer ? 1 : 2)) return JS_ThrowTypeError(ctx, "%s expects %d arguments", operation, observer ? 1 : 2);
    wifi_interface_t interface = WIFI_IF_STA;
    if (!observer && !tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    int32_t requested;
    if (!esp32_mquickjs_value_to_bounded_i32(ctx, argv[observer ? 0 : 1],
        observer ? -100 : interface == WIFI_IF_STA ? 3 : 10, observer ? 10 : UINT16_MAX, &requested)) {
        if (JS_HasException(ctx)) return JS_EXCEPTION;
        return JS_ThrowTypeError(ctx, observer ? "RSSI threshold must be an integer from -100 to 10 dBm"
            : "Inactive time must be integer seconds: Station 3..65535, AP 10..65535");
    }
    int32_t actual;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_apply_connection_control(control, interface, requested, &actual, &result);
    if (err != ESP_OK) return driver_phy_write_error(ctx, interface, operation, &result);
    return JS_NewInt32(ctx, actual);
}

JSValue js_wifi_driver_set_inactive_time(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_connection_control(ctx, argc, argv, ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME, "wifi.driver.setInactiveTime");
}

JSValue js_wifi_driver_set_rssi_threshold(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_connection_control(ctx, argc, argv, ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD, "wifi.driver.setRssiThreshold");
}

static JSValue driver_change_band(JSContext *ctx, int argc, JSValue *argv, bool band_mode)
{
    const char *operation = band_mode ? "wifi.driver.setBandMode" : "wifi.driver.setBand";
    static const char *const bands[] = {"2.4GHz", "5GHz"};
    static const char *const modes[] = {"2.4GHz-only", "5GHz-only", "auto"};
    size_t choice;
    if (argc != 1 || !esp32_mquickjs_value_to_enum(ctx, argv[0], band_mode ? modes : bands, band_mode ? 3 : 2, &choice)) {
        if (JS_HasException(ctx)) return JS_EXCEPTION;
        return JS_ThrowTypeError(ctx, "%s expects one exact band name", operation);
    }
    int32_t requested = band_mode ? (choice == 0 ? WIFI_BAND_MODE_2G_ONLY : choice == 1 ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_AUTO)
        : (choice == 0 ? WIFI_BAND_2G : WIFI_BAND_5G);
    int32_t actual;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_apply_band(band_mode, requested, &actual, &result);
    if (err != ESP_OK) return driver_phy_write_error(ctx, WIFI_IF_STA, operation, &result);
    return driver_scalar_to_js(ctx, band_mode ? ESP32_MQUICKJS_WIFI_DRIVER_BAND_MODE : ESP32_MQUICKJS_WIFI_DRIVER_BAND, actual);
}

JSValue js_wifi_driver_set_band(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_change_band(ctx, argc, argv, false);
}

JSValue js_wifi_driver_set_band_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return driver_change_band(ctx, argc, argv, true);
}

JSValue js_wifi_driver_tx_rate_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.driver.txRateStatus(interface) expects one argument");
    wifi_interface_t interface;
    if (!tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_tx_rate_record_t record = {0};
    uint32_t generation = 0;
    esp32_mquickjs_wifi_tx_rate_lease_t temporary = {0};
    esp_err_t err = esp32_mquickjs_wifi_radio_tx_rate_status(interface, &record, &generation, &temporary);
    if (err != ESP_OK) {
        esp32_mquickjs_wifi_tx_rate_write_t write = {0};
        return tx_rate_error(ctx, "wifi.driver.txRateStatus", err, &write, interface, &record, generation, &temporary);
    }
    return tx_rate_status_to_js(ctx, interface, &record, generation, &temporary);
}

JSValue js_wifi_driver_configure_tx_rate(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 2) return JS_ThrowTypeError(ctx, "wifi.driver.configureTxRate(interface, config) expects two arguments");
    wifi_interface_t interface;
    if (!tx_rate_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    wifi_tx_rate_config_t config;
    if (!esp32_mquickjs_wifi_tx_rate_capture(ctx, argv[1], &config)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_tx_rate_write_t write;
    esp32_mquickjs_wifi_tx_rate_record_t record;
    uint32_t generation;
    esp32_mquickjs_wifi_tx_rate_lease_t temporary;
    esp_err_t err = esp32_mquickjs_wifi_radio_configure_tx_rate(interface, &config, &write, &record, &generation, &temporary);
    if (err != ESP_OK) return tx_rate_error(ctx, "wifi.driver.configureTxRate", err,
        &write, interface, &record, generation, &temporary);
    /* A JS allocation failure here cannot undo the accepted native write.
     * txRateStatus remains available to inspect it before any retry. */
    return tx_rate_status_to_js(ctx, interface, &record, generation, &temporary);
}
#endif
