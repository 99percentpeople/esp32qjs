/* cutils branch hints must precede the IDF headers. */
#include "cutils.h"
#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_wifi_wapi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp32_mquickjs_wifi_config_fields.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include <string.h>
#include <math.h>

JSValue esp32_mquickjs_wifi_ssid_text(JSContext *ctx, const uint8_t *ssid, size_t length)
{
    if (ssid == NULL || length > 32) return JS_ThrowInternalError(ctx, "invalid native SSID span");
    for (size_t offset = 0; offset < length;) {
        size_t consumed;
        int codepoint = unicode_from_utf8(ssid + offset, length - offset, &consumed);
        if (codepoint < 0 || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return JS_NULL;
        offset += consumed;
    }
    return JS_NewStringLen(ctx, (const char *)ssid, length);
}

bool esp32_mquickjs_wifi_set_ssid_properties(JSContext *ctx, JSValue *object,
    const uint8_t *ssid, size_t length)
{
    if (ssid == NULL || length > 32) {
        JS_ThrowInternalError(ctx, "invalid native SSID span");
        return false;
    }
    JSGCRef bytes_ref;
    JSValue *bytes = JS_PushGCRef(ctx, &bytes_ref);
    *bytes = JS_NewArray(ctx, 0);
    bool ok = false;
    if (JS_IsException(*bytes)) goto done;
    for (uint32_t i = 0; i < length; ++i) {
        JSValue value = JS_NewUint32(ctx, ssid[i]);
        if (JS_IsException(value) || JS_IsException(JS_SetPropertyUint32(ctx, *bytes, i, value))) goto done;
    }
    ok = esp32_mquickjs_set_property_ref(ctx, object, "ssid", esp32_mquickjs_wifi_ssid_text(ctx, ssid, length)) &&
        esp32_mquickjs_set_property_ref(ctx, object, "ssidBytes", *bytes);
done:
    JS_PopGCRef(ctx, &bytes_ref);
    return ok;
}

/* Bounded copy before any driver call. Neither moving JS storage nor a native
 * ByteView read lease escapes capture. Strings retain the text API's NUL rule;
 * binary AP SSIDs use the SDK's explicit length and may contain zero bytes. */
static bool wifi_capture_config_ssid(JSContext *ctx, JSValue input, bool ap,
    uint8_t output[32], size_t *output_length)
{
    JSGCRef input_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &input_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    JSCStringBuf buffer;
    uint8_t bytes[32] = {0};
    size_t length = 0;
    bool ok = false;
    *root = input;
    if (JS_IsString(ctx, *root)) {
        const char *text = JS_ToCStringLen(ctx, &length, *root, &buffer);
        if (text == NULL) goto done;
        if (length == 0 || length > sizeof(bytes) || memchr(text, 0, length) != NULL) goto invalid;
        memcpy(bytes, text, length);
    } else if (JS_GetClassID(ctx, *root) == JS_CLASS_BYTE_VIEW) {
        const uint8_t *data = NULL;
        if (!esp32_mquickjs_byte_view_acquire_read(ctx, *root, "Wi-Fi config SSID", &data, &length)) goto done;
        bool valid = data != NULL && length > 0 && length <= sizeof(bytes) && (ap || memchr(data, 0, length) == NULL);
        if (valid) memcpy(bytes, data, length);
        esp32_mquickjs_byte_view_release_read(ctx, *root);
        if (!valid) goto invalid;
    } else {
        uint32_t count, byte;
        if (JS_GetClassID(ctx, *root) < 0) goto invalid;
        *value = JS_GetPropertyStr(ctx, *root, "length");
        if (JS_IsException(*value)) goto done;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, sizeof(bytes), &count)) goto invalid;
        length = count;
        for (uint32_t i = 0; i < count; ++i) {
            *value = JS_GetPropertyUint32(ctx, *root, i);
            if (JS_IsException(*value)) goto done;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 255, &byte) || (!ap && byte == 0)) goto invalid;
            bytes[i] = (uint8_t)byte;
        }
    }
    memcpy(output, bytes, length);
    *output_length = length;
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Wi-Fi config SSID expects 1..32 valid text or source bytes");
done:
    esp32_mquickjs_wireless_secure_zero(bytes, sizeof(bytes));
    esp32_mquickjs_wireless_secure_zero(&buffer, sizeof(buffer));
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &input_ref);
    return ok;
}

/* Capture each user value once into rooted plain storage, then run the same
 * Station validator used by raw configure. No second precedence/security table
 * and no partial native config is handed to a driver on capture failure. */
bool esp32_mquickjs_wifi_parse_connect_config(JSContext *ctx, int argc,
    JSGCRef *argv, wifi_config_t *config, uint32_t *timeout_ms)
{
    if (argc < 1 || argc > 2)
        return esp32_mquickjs_wifi_parse_station_config(ctx, argc, argv, config, timeout_ms);
    const char *keys[sizeof(wifi_station_driver_keys) / sizeof(wifi_station_driver_keys[0]) + 1];
    size_t count = 0;
    for (size_t i = 0; i < sizeof(wifi_station_driver_keys) / sizeof(wifi_station_driver_keys[0]); ++i)
        if (strcmp(wifi_station_driver_keys[i], "ssid") != 0)
            keys[count++] = wifi_station_driver_keys[i];
    size_t driver_count = count;
    keys[count++] = "timeoutMs";
    keys[count++] = "driver";
    JSGCRef options_ref, driver_ref, top_ref, nested_ref, arguments[2];
    JSValue *options = JS_PushGCRef(ctx, &options_ref);
    JSValue *driver = JS_PushGCRef(ctx, &driver_ref);
    JSValue *top = JS_PushGCRef(ctx, &top_ref);
    JSValue *nested = JS_PushGCRef(ctx, &nested_ref);
    JSValue *ssid = JS_PushGCRef(ctx, &arguments[0]);
    JSValue *merged = JS_PushGCRef(ctx, &arguments[1]);
    *ssid = argv[0].val;
    *options = argc == 2 ? argv[1].val : JS_NewObject(ctx);
    uint8_t ssid_bytes[32] = {0};
    size_t ssid_length = 0;
    bool ok = false;
    if (JS_IsException(*options) ||
        !wifi_capture_config_ssid(ctx, *ssid, false, ssid_bytes, &ssid_length) ||
        !esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.connect", keys, count)) goto done;
    /* Shared text parser validates the policy; bytes never pass through UTF-8. */
    *ssid = JS_NewString(ctx, "config");
    if (JS_IsException(*ssid)) goto done;
    *driver = JS_GetPropertyStr(ctx, *options, "driver");
    if (JS_IsException(*driver)) goto done;
    if (!JS_IsUndefined(*driver) &&
        !esp32_mquickjs_validate_plain_options(ctx, *driver, "wifi.connect driver", keys, driver_count)) goto done;
    *merged = JS_NewObject(ctx);
    if (JS_IsException(*merged)) goto done;
    for (size_t i = 0; i < driver_count; ++i) {
        *top = JS_GetPropertyStr(ctx, *options, keys[i]);
        if (JS_IsException(*top)) goto done;
        *nested = JS_IsUndefined(*driver) ? JS_UNDEFINED : JS_GetPropertyStr(ctx, *driver, keys[i]);
        if (JS_IsException(*nested)) goto done;
        if (!JS_IsUndefined(*top) && !JS_IsUndefined(*nested)) {
            JS_ThrowTypeError(ctx, "wifi.connect option '%s' is present in both options and driver", keys[i]);
            goto done;
        }
        if (!JS_IsUndefined(*nested)) *top = *nested;
        if (!JS_IsUndefined(*top) && JS_IsException(JS_SetPropertyStr(ctx, *merged, keys[i], *top))) goto done;
    }
    *top = JS_GetPropertyStr(ctx, *options, "timeoutMs");
    if (JS_IsException(*top)) goto done;
    if (!JS_IsUndefined(*top) && JS_IsException(JS_SetPropertyStr(ctx, *merged, "timeoutMs", *top))) goto done;
    ok = esp32_mquickjs_wifi_parse_station_config(ctx, 2, arguments, config, timeout_ms);
    if (ok) {
        memset(config->sta.ssid, 0, sizeof(config->sta.ssid));
        memcpy(config->sta.ssid, ssid_bytes, ssid_length);
    }
done:
    esp32_mquickjs_wireless_secure_zero(ssid_bytes, sizeof(ssid_bytes));
    if (!ok) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    JS_PopGCRef(ctx, &arguments[1]);
    JS_PopGCRef(ctx, &arguments[0]);
    JS_PopGCRef(ctx, &nested_ref);
    JS_PopGCRef(ctx, &top_ref);
    JS_PopGCRef(ctx, &driver_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}

#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
/* Keep the ergonomic and raw units distinct. Only the raw channel/TU fields
 * bypass the text parser's narrower input ranges, then the full native AP
 * validator checks the final config before any lifecycle/driver operation. */
bool esp32_mquickjs_wifi_parse_start_ap_config(JSContext *ctx, JSValue input,
    wifi_config_t *config, bool *allow_disconnect)
{
    const char *outer_keys[sizeof(wifi_ap_driver_keys) / sizeof(wifi_ap_driver_keys[0]) + 2];
    const char *nested_keys[sizeof(wifi_ap_driver_keys) / sizeof(wifi_ap_driver_keys[0])];
    size_t outer_count = 0, nested_count = 0;
    for (size_t i = 0; i < sizeof(wifi_ap_driver_keys) / sizeof(wifi_ap_driver_keys[0]); ++i) {
        const char *key = wifi_ap_driver_keys[i];
        outer_keys[outer_count++] = strcmp(key, "beaconIntervalTu") == 0 ? "beaconIntervalMs" : key;
        if (strcmp(key, "ssid") != 0) nested_keys[nested_count++] = key;
    }
    outer_keys[outer_count++] = "driver";
    outer_keys[outer_count++] = "allowDisconnect";
    JSGCRef input_ref, driver_ref, top_ref, nested_ref, merged_ref;
    JSValue *options = JS_PushGCRef(ctx, &input_ref);
    JSValue *driver = JS_PushGCRef(ctx, &driver_ref);
    JSValue *top = JS_PushGCRef(ctx, &top_ref);
    JSValue *nested = JS_PushGCRef(ctx, &nested_ref);
    JSValue *merged = JS_PushGCRef(ctx, &merged_ref);
    *options = input;
    bool ok = false, raw_beacon = false, raw_channel = false, disconnect = false;
    *allow_disconnect = false;
    uint32_t beacon_tu = 0, channel = 0;
    uint8_t ssid_bytes[32] = {0};
    size_t ssid_length = 0;
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.startAP", outer_keys, outer_count)) goto done;
    *top = JS_GetPropertyStr(ctx, *options, "allowDisconnect");
    if (JS_IsException(*top)) goto done;
    if (!JS_IsUndefined(*top)) {
        if (!JS_IsBool(*top)) goto invalid;
        disconnect = *top == JS_TRUE;
    }
    *driver = JS_GetPropertyStr(ctx, *options, "driver");
    if (JS_IsException(*driver)) goto done;
    if (!JS_IsUndefined(*driver) &&
        !esp32_mquickjs_validate_plain_options(ctx, *driver, "wifi.startAP driver", nested_keys, nested_count)) goto done;
    *merged = JS_NewObject(ctx);
    if (JS_IsException(*merged)) goto done;
    for (size_t i = 0; i < sizeof(wifi_ap_driver_keys) / sizeof(wifi_ap_driver_keys[0]); ++i) {
        const char *key = wifi_ap_driver_keys[i];
        bool beacon = strcmp(key, "beaconIntervalTu") == 0;
        const char *top_key = beacon ? "beaconIntervalMs" : key;
        *top = JS_GetPropertyStr(ctx, *options, top_key);
        if (JS_IsException(*top)) goto done;
        if (strcmp(key, "ssid") == 0) {
            if (!wifi_capture_config_ssid(ctx, *top, true, ssid_bytes, &ssid_length)) goto done;
            *top = JS_NewString(ctx, "config");
            if (JS_IsException(*top)) goto done;
        }
        *nested = JS_IsUndefined(*driver) || strcmp(key, "ssid") == 0
            ? JS_UNDEFINED : JS_GetPropertyStr(ctx, *driver, key);
        if (JS_IsException(*nested)) goto done;
        if (!JS_IsUndefined(*top) && !JS_IsUndefined(*nested)) {
            JS_ThrowTypeError(ctx, "wifi.startAP option '%s' is present in both options and driver", top_key);
            goto done;
        }
        if (!JS_IsUndefined(*nested)) {
            if (beacon) {
                if (!esp32_mquickjs_value_to_bounded_u32(ctx, *nested,
                    ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU, ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU, &beacon_tu) ||
                    beacon_tu % ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU != 0) goto invalid;
                raw_beacon = true;
                continue;
            }
            if (strcmp(key, "channel") == 0) {
                if (!esp32_mquickjs_value_to_bounded_u32(ctx, *nested, 0, 177, &channel)) goto invalid;
                raw_channel = true;
                continue;
            }
            /* Preserve raw-config empty-password semantics. Nonempty values
             * still go through the shared authentication/password validator. */
            if (strcmp(key, "password") == 0 && JS_IsString(ctx, *nested)) {
                JSCStringBuf buffer;
                size_t length = 0;
                bool converted = JS_ToCStringLen(ctx, &length, *nested, &buffer) != NULL;
                esp32_mquickjs_wireless_secure_zero(&buffer, sizeof(buffer));
                if (!converted) goto done;
                if (length == 0) continue;
            }
            *top = *nested;
        }
        if (!JS_IsUndefined(*top) && JS_IsException(JS_SetPropertyStr(ctx, *merged, top_key, *top))) goto done;
    }
    if (!esp32_mquickjs_wifi_parse_ap_config_for_operation(ctx, *merged, config, "wifi.startAP")) goto done;
    memset(config->ap.ssid, 0, sizeof(config->ap.ssid));
    memcpy(config->ap.ssid, ssid_bytes, ssid_length);
    config->ap.ssid_len = (uint8_t)ssid_length;
    if (raw_beacon) config->ap.beacon_interval = (uint16_t)beacon_tu;
    if (raw_channel) config->ap.channel = (uint8_t)channel;
    esp_err_t validation = esp32_mquickjs_wifi_radio_validate_ap_config(config);
    if (validation == ESP_ERR_NOT_SUPPORTED) {
        (void)esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_AP_UNSUPPORTED", "wifi.startAP", validation, -1, UINT32_MAX);
        goto done;
    }
    if (validation != ESP_OK) goto invalid;
    *allow_disconnect = disconnect;
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid or conflicting wifi.startAP driver options");
done:
    if (!ok) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    esp32_mquickjs_wireless_secure_zero(ssid_bytes, sizeof(ssid_bytes));
    JS_PopGCRef(ctx, &merged_ref);
    JS_PopGCRef(ctx, &nested_ref);
    JS_PopGCRef(ctx, &top_ref);
    JS_PopGCRef(ctx, &driver_ref);
    JS_PopGCRef(ctx, &input_ref);
    return ok;
}
#endif

static bool wifi_driver_config_unsupported(JSContext *ctx, const char *operation, wifi_interface_t interface)
{
    if (strcmp(operation, "wifi.configure") == 0)
        (void)esp32_mquickjs_wifi_throw_configuration_error(ctx, ESP_ERR_NOT_SUPPORTED, NULL, NULL);
    else
        (void)esp32_mquickjs_wifi_throw_operation_error(ctx,
            interface == WIFI_IF_AP ? "WIFI_AP_UNSUPPORTED" : "WIFI_CONNECT_UNSUPPORTED",
            operation, ESP_ERR_NOT_SUPPORTED, -1, UINT32_MAX);
    return false;
}

bool esp32_mquickjs_wifi_parse_driver_config_for_operation(JSContext *ctx, JSValue options,
    wifi_interface_t interface, wifi_config_t *config, const char *operation)
{
    if (config != NULL) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    if (config == NULL || (interface != WIFI_IF_STA && interface != WIFI_IF_AP)) {
        JS_ThrowTypeError(ctx, "invalid Wi-Fi configuration interface");
        return false;
    }
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) {
        return wifi_driver_config_unsupported(ctx, operation, interface);
    }
#endif
    bool ap = interface == WIFI_IF_AP;
    const char *const *keys = ap ? wifi_ap_driver_keys : wifi_station_driver_keys;
    size_t key_count = ap ? sizeof(wifi_ap_driver_keys) / sizeof(wifi_ap_driver_keys[0]) :
        sizeof(wifi_station_driver_keys) / sizeof(wifi_station_driver_keys[0]);
    JSGCRef options_ref, value_ref, args[2];
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    JSValue *ssid_argument = JS_PushGCRef(ctx, &args[0]);
    JSValue *translated = JS_PushGCRef(ctx, &args[1]);
    uint8_t ssid[32] = {0};
    size_t ssid_length = 0;
    uint32_t beacon_tu = ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU, ap_channel = 0;
    uint32_t timeout_ms;
    bool ok = false;
    *root = options;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "Wi-Fi driver config", keys, key_count)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "ssid");
    if (JS_IsException(*value) || !wifi_capture_config_ssid(ctx, *value, ap, ssid, &ssid_length)) goto done;
    *translated = JS_NewObject(ctx);
    if (JS_IsException(*translated)) goto done;
    for (size_t i = 0; i < key_count; ++i) {
        const char *key = keys[i];
        if (strcmp(key, "ssid") == 0) continue;
        *value = JS_GetPropertyStr(ctx, *root, key);
        if (JS_IsException(*value)) goto done;
        if (JS_IsUndefined(*value)) continue;
        if (ap && strcmp(key, "password") == 0 && JS_IsString(ctx, *value)) {
            JSCStringBuf scratch;
            size_t length = 0;
            const char *text = JS_ToCStringLen(ctx, &length, *value, &scratch);
            bool converted = text != NULL;
            esp32_mquickjs_wireless_secure_zero(&scratch, sizeof(scratch));
            if (!converted) goto done;
            /* Raw empty credentials mean the same as an absent password.
             * The shared AP validator still rejects a secured auth mode. */
            if (length == 0) continue;
        }
        if (ap && strcmp(key, "beaconIntervalTu") == 0) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value,
                ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU, ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU, &beacon_tu) ||
                beacon_tu % ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU != 0) goto invalid;
            continue;
        }
        if (ap && strcmp(key, "channel") == 0) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 177, &ap_channel) ||
                (ap_channel > 14 && esp32_mquickjs_wifi_radio_5ghz_channel_bit((uint8_t)ap_channel) == 0)) goto invalid;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
            if (ap_channel > 14) {
                (void)wifi_driver_config_unsupported(ctx, operation, interface);
                goto done;
            }
#endif
            continue;
        }
        if (JS_IsException(JS_SetPropertyStr(ctx, *translated, key, *value))) goto done;
    }
    /* Reuse the reviewed personal-security/PHY validators. The temporary SSID
     * is never sent to a driver. Copy the already captured byte identity only
     * after validation; no raw bytes round-trip through UTF-8 decoding. */
    *ssid_argument = JS_NewString(ctx, "config");
    if (JS_IsException(*ssid_argument)) goto done;
    if (ap) {
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (JS_IsException(JS_SetPropertyStr(ctx, *translated, "ssid", *ssid_argument)) ||
            !esp32_mquickjs_wifi_parse_ap_config_for_operation(ctx, *translated, config, operation)) goto done;
        memset(config->ap.ssid, 0, sizeof(config->ap.ssid));
        memcpy(config->ap.ssid, ssid, ssid_length);
        config->ap.ssid_len = ssid_length;
        config->ap.channel = ap_channel;
        config->ap.beacon_interval = beacon_tu;
        esp_err_t validation = esp32_mquickjs_wifi_radio_validate_ap_config(config);
        if (validation == ESP_ERR_NOT_SUPPORTED) {
            (void)wifi_driver_config_unsupported(ctx, operation, interface);
            goto done;
        }
        if (validation != ESP_OK) goto invalid;
#endif
    } else {
        if (!esp32_mquickjs_wifi_parse_station_config_for_operation(ctx, 2, args, config, &timeout_ms, operation)) goto done;
        memset(config->sta.ssid, 0, sizeof(config->sta.ssid));
        memcpy(config->sta.ssid, ssid, ssid_length);
    }
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid or conflicting Wi-Fi driver config");
done:
    if (!ok) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    esp32_mquickjs_wireless_secure_zero(ssid, sizeof(ssid));
    JS_PopGCRef(ctx, &args[1]);
    JS_PopGCRef(ctx, &args[0]);
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}
bool esp32_mquickjs_wifi_parse_driver_config(JSContext *ctx, JSValue options,
    wifi_interface_t interface, wifi_config_t *config)
{
    return esp32_mquickjs_wifi_parse_driver_config_for_operation(ctx, options, interface, config, "wifi.configure");
}

/* Capture only: these helpers never call a native driver or acquire owners. */
static bool wifi_configuration_unsupported(JSContext *ctx)
{
    esp32_mquickjs_wifi_throw_configuration_error(ctx, ESP_ERR_NOT_SUPPORTED, NULL, NULL);
    return false;
}

static bool wifi_configuration_bool(JSContext *ctx, JSValue object, const char *key,
    bool *present, bool *output)
{
    JSValue value = JS_GetPropertyStr(ctx, object, key);
    if (JS_IsException(value)) return false;
    *present = !JS_IsUndefined(value);
    if (!*present) return true;
    if (!JS_IsBool(value)) return false;
    *output = value == JS_TRUE;
    return true;
}

static bool wifi_capture_configuration_country(JSContext *ctx, JSValue input,
    esp32_mquickjs_wifi_radio_config_controls_t *controls, bool details_only)
{
    static const char *const keys[] = {"code", "policy", "startChannel", "channelCount",
        "environment", "ghz5ChannelMask"};
    static const char *const policies[] = {"auto", "manual"};
    static const char *const environments[] = {"indoor", "outdoor", "X"};
    JSGCRef root_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    JSCStringBuf buffer;
    bool ok = false, object = !JS_IsString(ctx, input);
    bool first = false, count = false, environment = false, mask = false;
    uint32_t number;
    size_t length, choice;
    *root = input;
    if (details_only && !object) goto done;
    if (object && !esp32_mquickjs_validate_plain_options(ctx, *root,
        details_only ? "wifi.driver.setCountryDetails" : "wifi.configure country",
        keys, sizeof(keys) / sizeof(keys[0]))) goto done;
    *value = object ? JS_GetPropertyStr(ctx, *root, "code") : *root;
    const char *code;
    if (!JS_IsString(ctx, *value) || (code = JS_ToCStringLen(ctx, &length, *value, &buffer)) == NULL ||
        length != 2 || !((code[0] >= 'A' && code[0] <= 'Z' && code[1] >= 'A' && code[1] <= 'Z') ||
                        (code[0] == '0' && code[1] == '1'))) goto done;
    memcpy(controls->country.cc, code, 2);
    controls->country.policy = WIFI_COUNTRY_POLICY_AUTO;
    if (object) {
        *value = JS_GetPropertyStr(ctx, *root, "policy");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value)) {
            if (!esp32_mquickjs_value_to_enum(ctx, *value, policies, 2, &choice)) goto done;
            controls->country.policy = choice == 0 ? WIFI_COUNTRY_POLICY_AUTO : WIFI_COUNTRY_POLICY_MANUAL;
        }
        *value = JS_GetPropertyStr(ctx, *root, "startChannel");
        if (JS_IsException(*value)) goto done;
        first = !JS_IsUndefined(*value);
        if (first) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 14, &number)) goto done;
            controls->country.schan = number;
        }
        *value = JS_GetPropertyStr(ctx, *root, "channelCount");
        if (JS_IsException(*value)) goto done;
        count = !JS_IsUndefined(*value);
        if (count) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 14, &number)) goto done;
            controls->country.nchan = number;
        }
        *value = JS_GetPropertyStr(ctx, *root, "environment");
        if (JS_IsException(*value)) goto done;
        environment = !JS_IsUndefined(*value);
        if (environment && !JS_IsNull(*value)) {
            if (!esp32_mquickjs_value_to_enum(ctx, *value, environments, 3, &choice)) goto done;
            controls->country.cc[2] = choice == 0 ? 'I' : choice == 1 ? 'O' : 'X';
        }
        *value = JS_GetPropertyStr(ctx, *root, "ghz5ChannelMask");
        if (JS_IsException(*value)) goto done;
        mask = !JS_IsUndefined(*value);
        if (mask) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, UINT32_MAX, &number)) goto done;
#if CONFIG_SOC_WIFI_SUPPORT_5G
            controls->country.wifi_5g_channel_mask = number;
#else
            if (details_only) JS_ThrowTypeError(ctx, "5 GHz country mask requires a 5 GHz Wi-Fi target");
            else (void)wifi_configuration_unsupported(ctx);
            goto done;
#endif
        }
    }
    bool details = first || count || environment || mask;
    if (details_only && !details) goto done;
    if (details && (!first || !count)) goto done;
    controls->country_by_code = !details;
    controls->country_set = true;
    ok = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &root_ref);
    return ok;
}

bool esp32_mquickjs_wifi_capture_country_details(JSContext *ctx, JSValue input, wifi_country_t *country)
{
    if (country == NULL) return false;
    memset(country, 0, sizeof(*country));
    esp32_mquickjs_wifi_radio_config_controls_t controls = {0};
    if (!wifi_capture_configuration_country(ctx, input, &controls, true) ||
        esp32_mquickjs_wifi_radio_validate_config_controls(WIFI_MODE_STA, &controls) != ESP_OK) {
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Invalid country details; startChannel and channelCount are required");
        return false;
    }
    *country = controls.country;
    return true;
}

bool esp32_mquickjs_wifi_capture_protocol(JSContext *ctx, JSValue input, uint16_t *output)
{
    static const char *const names[] = {"11b", "11g", "11n", "lr", "11a", "11ac", "11ax"};
    static const uint16_t values[] = {WIFI_PROTOCOL_11B, WIFI_PROTOCOL_11G, WIFI_PROTOCOL_11N,
        WIFI_PROTOCOL_LR, WIFI_PROTOCOL_11A, WIFI_PROTOCOL_11AC, WIFI_PROTOCOL_11AX};
    JSGCRef root_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    bool ok = false;
    uint16_t bitmap = 0;
    uint32_t length;
    *root = input;
    if (!JS_IsArray(ctx, *root)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "length");
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 7, &length)) goto done;
    for (uint32_t i = 0; i < length; ++i) {
        size_t choice;
        *value = JS_GetPropertyUint32(ctx, *root, i);
        if (!esp32_mquickjs_value_to_enum(ctx, *value, names, 7, &choice) || (bitmap & values[choice])) goto done;
        bitmap |= values[choice];
    }
    *output = bitmap;
    ok = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &root_ref);
    return ok;
}

static bool wifi_capture_configuration_phy(JSContext *ctx, JSValue input, bool bandwidth,
    esp32_mquickjs_wifi_radio_config_controls_t *controls)
{
    static const char *const interfaces[] = {"station", "access-point"};
    static const char *const protocols[] = {"ghz2", "ghz5"};
    static const char *const widths[] = {"ghz2MHz", "ghz5MHz"};
    const char *const *keys = bandwidth ? widths : protocols;
    JSGCRef root_ref, interface_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref);
    JSValue *object = JS_PushGCRef(ctx, &interface_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    bool ok = false, any = false;
    *root = input;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.configure interfaces", interfaces, 2)) goto done;
    for (size_t i = 0; i < 2; ++i) {
        *object = JS_GetPropertyStr(ctx, *root, interfaces[i]);
        if (JS_IsException(*object)) goto done;
        if (JS_IsUndefined(*object)) continue;
        if (!esp32_mquickjs_validate_plain_options(ctx, *object, "wifi.configure PHY", keys, 2)) goto done;
        uint8_t bands = 0;
        for (size_t band = 0; band < 2; ++band) {
            *value = JS_GetPropertyStr(ctx, *object, keys[band]);
            if (JS_IsException(*value)) goto done;
            if (JS_IsUndefined(*value)) continue;
            bands |= 1U << band;
            if (bandwidth) {
                uint32_t number;
                if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 20, 40, &number) ||
                    (number != 20 && number != 40)) goto done;
                wifi_bandwidth_t width = number == 20 ? WIFI_BW20 : WIFI_BW40;
                if (band == 0) controls->bandwidths[i].ghz_2g = width;
                else controls->bandwidths[i].ghz_5g = width;
            } else {
                uint16_t bitmap;
                if (!esp32_mquickjs_wifi_capture_protocol(ctx, *value, &bitmap)) goto done;
                if (band == 0) controls->protocols[i].ghz_2g = bitmap;
                else controls->protocols[i].ghz_5g = bitmap;
            }
        }
        if (bands == 0) goto done;
        if (bandwidth) controls->bandwidth_bands[i] = bands;
        else controls->protocol_bands[i] = bands;
        any = true;
    }
    ok = any;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &interface_ref);
    JS_PopGCRef(ctx, &root_ref);
    return ok;
}

bool esp32_mquickjs_wifi_capture_stop_ap_timeout(JSContext *ctx, JSValue value, uint32_t *timeout_ms)
{
    if (timeout_ms == NULL) {
        JS_ThrowTypeError(ctx, "missing native Wi-Fi AP stop timeout");
        return false;
    }
    *timeout_ms = 0;
    if (JS_IsUndefined(value)) { *timeout_ms = 1000; return true; }
    uint32_t captured;
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, value, 1, 60000, &captured)) {
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "wifi.stopAP timeoutMs expects an integer from 1 to 60000");
        return false;
    }
    *timeout_ms = captured;
    return true;
}

static bool wifi_capture_lifecycle_timeout(JSContext *ctx, JSValue options,
    const char *operation, uint32_t default_ms, uint32_t *timeout_ms)
{
    static const char *const keys[] = {"timeoutMs"};
    if (timeout_ms == NULL) {
        JS_ThrowTypeError(ctx, "missing native Wi-Fi lifecycle timeout");
        return false;
    }
    *timeout_ms = 0;
    if (JS_IsUndefined(options)) { *timeout_ms = default_ms; return true; }
    JSGCRef options_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *root = options;
    bool ok = false;
    uint32_t captured = default_ms;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, operation, keys, 1)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "timeoutMs");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value) &&
        !esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 60000, &captured)) {
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "%s timeoutMs expects an integer from 1 to 60000", operation);
        goto done;
    }
    *timeout_ms = captured;
    ok = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}

bool esp32_mquickjs_wifi_capture_stop(JSContext *ctx, JSValue options, uint32_t *timeout_ms)
{
    return wifi_capture_lifecycle_timeout(ctx, options, "wifi.stop", 1000, timeout_ms);
}

bool esp32_mquickjs_wifi_capture_restart(JSContext *ctx, JSValue options, uint32_t *timeout_ms, bool *allow_ap_restart)
{
    static const char *const keys[] = {"timeoutMs", "allowApRestart"};
    if (timeout_ms == NULL || allow_ap_restart == NULL) {
        JS_ThrowTypeError(ctx, "missing native Wi-Fi restart options");
        return false;
    }
    *timeout_ms = 0;
    *allow_ap_restart = false;
    if (JS_IsUndefined(options)) { *timeout_ms = 10000; return true; }
    JSGCRef options_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *root = options;
    bool ok = false, captured_ap = false;
    uint32_t captured = 10000;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.driver.restart", keys, 2)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "timeoutMs");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value) &&
        !esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 60000, &captured)) {
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "wifi.driver.restart timeoutMs expects an integer from 1 to 60000");
        goto done;
    }
    *value = JS_GetPropertyStr(ctx, *root, "allowApRestart");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!JS_IsBool(*value)) {
            JS_ThrowTypeError(ctx, "wifi.driver.restart allowApRestart expects a boolean");
            goto done;
        }
        captured_ap = *value == JS_TRUE;
    }
    *timeout_ms = captured;
    *allow_ap_restart = captured_ap;
    ok = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}

#if CONFIG_ESP_WIFI_WAPI_PSK
bool esp32_mquickjs_wifi_capture_wapi_control(JSContext *ctx, JSValue options, bool enable, uint32_t *timeout_ms)
{
    return wifi_capture_lifecycle_timeout(ctx, options, enable ? "wifi.wapi.enable" : "wifi.wapi.disable", 10000, timeout_ms);
}
#endif

bool esp32_mquickjs_wifi_capture_start(JSContext *ctx, JSValue options,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection)
{
    static const char *const keys[] = {"mode", "storage"};
    static const char *const modes[] = {"station", "ap", "apsta"};
    static const wifi_mode_t mode_values[] = {WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA};
    static const char *const storages[] = {"ram", "flash"};
    if (selection == NULL) {
        JS_ThrowTypeError(ctx, "missing native Wi-Fi start selection");
        return false;
    }
    memset(selection, 0, sizeof(*selection));
    selection->start_only = true;
    selection->start = selection->start_set = true;
    if (JS_IsUndefined(options)) return true;
    JSGCRef options_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *root = options;
    bool ok = false;
    size_t choice;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.start", keys, 2)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "mode");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *value, modes, 3, &choice)) {
            if (!JS_HasException(ctx))
                JS_ThrowTypeError(ctx, "wifi.start mode must be station, ap or apsta");
            goto done;
        }
        selection->mode_set = true;
        selection->mode = mode_values[choice];
    }
    *value = JS_GetPropertyStr(ctx, *root, "storage");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *value, storages, 2, &choice)) {
            if (!JS_HasException(ctx))
                JS_ThrowTypeError(ctx, "wifi.start storage must be ram or flash");
            goto done;
        }
        selection->storage_set = true;
        selection->storage = choice == 0 ? WIFI_STORAGE_RAM : WIFI_STORAGE_FLASH;
    }
    ok = true;
done:
    if (!ok) memset(selection, 0, sizeof(*selection));
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}

bool esp32_mquickjs_wifi_capture_configuration(JSContext *ctx, JSValue options,
    esp32_mquickjs_wifi_configuration_t *configuration)
{
    static const char *const keys[] = {"mode", "storage", "start", "allowDisconnect", "station", "accessPoint",
        "country", "protocols", "bandwidths", "txPowerDbm", "powerSave"};
    static const char *const modes[] = {"station", "ap", "apsta"};
    static const wifi_mode_t mode_values[] = {WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA};
    static const char *const storage[] = {"ram", "flash"};
    static const char *const power_save[] = {"none", "minimum", "maximum"};
    static const wifi_ps_type_t power_values[] = {WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM};
    if (configuration == NULL) {
        JS_ThrowTypeError(ctx, "missing native Wi-Fi configuration storage");
        return false;
    }
    memset(configuration, 0, sizeof(*configuration));
    JSGCRef options_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    bool ok = false, present;
    size_t choice;
    *root = options;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.configure", keys, sizeof(keys) / sizeof(keys[0]))) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "mode");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *value, modes, 3, &choice)) goto done;
        configuration->mode_set = true;
        configuration->mode = mode_values[choice];
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (configuration->mode & WIFI_MODE_AP) { (void)wifi_configuration_unsupported(ctx); goto done; }
#endif
    }
    *value = JS_GetPropertyStr(ctx, *root, "storage");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *value, storage, 2, &choice)) goto done;
        configuration->storage_set = true;
        configuration->storage = choice == 0 ? WIFI_STORAGE_RAM : WIFI_STORAGE_FLASH;
    }
    if (!wifi_configuration_bool(ctx, *root, "start", &configuration->start_set, &configuration->start) ||
        !wifi_configuration_bool(ctx, *root, "allowDisconnect", &present, &configuration->allow_disconnect)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "country");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value) && !wifi_capture_configuration_country(ctx, *value, &configuration->controls, false)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "protocols");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value) && !wifi_capture_configuration_phy(ctx, *value, false, &configuration->controls)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "bandwidths");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value) && !wifi_capture_configuration_phy(ctx, *value, true, &configuration->controls)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "txPowerDbm");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        double power;
        if (!JS_IsNumber(ctx, *value) || JS_ToNumber(ctx, &power, *value) != 0 ||
            !isfinite(power) || power < 2 || power > 20 || floor(power * 4) != power * 4) goto done;
        configuration->start_controls.tx_power_set = true;
        configuration->start_controls.tx_power_quarter_dbm = (int8_t)(power * 4);
    }
    *value = JS_GetPropertyStr(ctx, *root, "powerSave");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *value, power_save, 3, &choice)) goto done;
        configuration->controls.power_save_set = true;
        configuration->controls.power_save = power_values[choice];
    }
    *value = JS_GetPropertyStr(ctx, *root, "station");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_wifi_parse_driver_config(ctx, *value, WIFI_IF_STA, &configuration->station)) goto done;
        configuration->station_set = true;
    }
    *value = JS_GetPropertyStr(ctx, *root, "accessPoint");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_wifi_parse_driver_config(ctx, *value, WIFI_IF_AP, &configuration->access_point)) goto done;
        configuration->access_point_set = true;
    }
    if (configuration->mode_set && ((configuration->station_set && !(configuration->mode & WIFI_MODE_STA)) ||
        (configuration->access_point_set && !(configuration->mode & WIFI_MODE_AP)))) goto done;
    /* Missing selections remain missing. APSTA here is only a validation
     * superset, not a default mode or permission to initialize an AP. */
    wifi_mode_t validation_mode = configuration->mode_set ? configuration->mode : WIFI_MODE_APSTA;
    esp_err_t validation = esp32_mquickjs_wifi_radio_validate_config_controls(validation_mode, &configuration->controls);
    if (validation == ESP_OK) validation = esp32_mquickjs_wifi_radio_validate_start_controls(
        !configuration->start_set || configuration->start, &configuration->start_controls);
    if (validation == ESP_ERR_NOT_SUPPORTED) { (void)wifi_configuration_unsupported(ctx); goto done; }
    if (validation != ESP_OK) goto done;
    ok = true;
done:
    if (!ok) {
        esp32_mquickjs_wireless_secure_zero(configuration, sizeof(*configuration));
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid or conflicting wifi.configure options");
    }
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}
#endif
