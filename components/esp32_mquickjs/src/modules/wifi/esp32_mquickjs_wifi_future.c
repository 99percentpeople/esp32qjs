#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_wireless_core.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_timer.h"

typedef enum {
    WIFI_FUTURE_SCAN,
    WIFI_FUTURE_CONNECT,
    WIFI_FUTURE_DISCONNECT,
} wifi_future_kind_t;

static const char *wifi_future_operation_name(wifi_future_kind_t kind)
{
    return kind == WIFI_FUTURE_SCAN
               ? "wifi.scan"
               : kind == WIFI_FUTURE_CONNECT ? "wifi.connect"
                                             : "wifi.disconnect";
}

struct esp32_mquickjs_future_driver_state {
    wifi_future_kind_t kind;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint32_t generation;
    uint32_t timeout_ms;
    uint32_t scan_status;
    uint32_t connect_kind;
    int32_t connect_reason;
    uint64_t connect_start_us;
    uint64_t connect_completed_us;
    esp32_mquickjs_wifi_link_snapshot_t connect_link;
    wifi_scan_config_t scan_config;
    uint8_t scan_ssid[33];
    uint8_t scan_bssid[6];
    uint16_t scan_max_records;
    wifi_config_t connect_config;
    bool started;
    bool completed;
    bool cancel_requested;
};

static bool wifi_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0 && !JS_IsArray(ctx, value);
}

static bool wifi_string_equals(JSContext *ctx, JSValue value,
                               const char *expected)
{
    JSCStringBuf buffer;
    const char *text;
    size_t length;

    return JS_IsString(ctx, value) &&
           (text = JS_ToCStringLen(ctx, &length, value, &buffer)) != NULL &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static bool wifi_validate_option_keys(JSContext *ctx, JSValue options,
                                      const char *api_name,
                                      const char *const *allowed,
                                      size_t allowed_count)
{
    return esp32_mquickjs_validate_plain_options(
        ctx, options, api_name, allowed, allowed_count);
}

static bool wifi_to_integer(JSContext *ctx, JSValue value,
                            int32_t minimum, int32_t maximum,
                            int32_t *out)
{
    double number;

    if (out == NULL || !JS_IsNumber(ctx, value) ||
        JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number) ||
        floor(number) != number || number < minimum || number > maximum) {
        return false;
    }
    *out = (int32_t)number;
    return true;
}

static bool wifi_parse_bssid(JSContext *ctx, JSValue value,
                             uint8_t output[6])
{
    JSCStringBuf buffer;
    const char *text;
    size_t length;
    size_t index;

    if (!JS_IsString(ctx, value) ||
        (text = JS_ToCStringLen(ctx, &length, value, &buffer)) == NULL || length != 17) {
        return false;
    }
    for (index = 0; index < 6; ++index) {
        uint8_t byte = 0;
        if (index != 0 && text[index * 3 - 1] != ':') return false;
        for (size_t digit = 0; digit < 2; ++digit) {
            char c = text[index * 3 + digit];
            unsigned value;
            if (c >= '0' && c <= '9') value = c - '0';
            else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
            else return false;
            byte = (uint8_t)((byte << 4) | value);
        }
        output[index] = byte;
    }
    return true;
}

static bool wifi_parse_auth_mode(JSContext *ctx, JSValue value,
                                 wifi_auth_mode_t *out)
{
    if (wifi_string_equals(ctx, value, "open")) {
        *out = WIFI_AUTH_OPEN;
    } else if (wifi_string_equals(ctx, value, "wep")) {
        *out = WIFI_AUTH_WEP;
    } else if (wifi_string_equals(ctx, value, "wpa")) {
        *out = WIFI_AUTH_WPA_PSK;
    } else if (wifi_string_equals(ctx, value, "wpa2")) {
        *out = WIFI_AUTH_WPA2_PSK;
    } else if (wifi_string_equals(ctx, value, "wpa/wpa2")) {
        *out = WIFI_AUTH_WPA_WPA2_PSK;
    } else if (wifi_string_equals(ctx, value, "wpa3")) {
        *out = WIFI_AUTH_WPA3_PSK;
    } else if (wifi_string_equals(ctx, value, "wpa2/wpa3")) {
        *out = WIFI_AUTH_WPA2_WPA3_PSK;
    } else if (wifi_string_equals(ctx, value, "wapi")) {
        *out = WIFI_AUTH_WAPI_PSK;
    } else if (wifi_string_equals(ctx, value, "owe")) {
        *out = WIFI_AUTH_OWE;
    } else {
        return false;
    }
    return true;
}

static bool wifi_station_unsupported(JSContext *ctx, const char *option, const char *operation)
{
    if (strcmp(operation, "wifi.configure") == 0) {
        (void)esp32_mquickjs_wifi_throw_configuration_error(ctx, ESP_ERR_NOT_SUPPORTED, option, NULL);
        return false;
    }
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (!JS_IsException(*details) &&
        esp32_mquickjs_set_property_ref(ctx, details, "option", JS_NewString(ctx, option)) &&
        esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, ESP_ERR_NOT_SUPPORTED)) &&
        esp32_mquickjs_set_property_ref(ctx, details, "espName", JS_NewString(ctx, esp_err_to_name(ESP_ERR_NOT_SUPPORTED)))) {
        (void)esp32_mquickjs_throw_native_error(ctx, "WIFI_CONNECT_UNSUPPORTED", operation,
            "requested station option is unavailable in this SDK/build", *details);
    }
    JS_PopGCRef(ctx, &details_ref);
    return false;
}

static bool wifi_parse_station_phy(JSContext *ctx, JSValue options, wifi_sta_config_t *sta, const char *operation)
{
    static const char *const names[] = {
        "heDcmSet", "heMcs9Enabled", "heSuBeamformeeDisabled",
        "heTrigSuBeamformingFeedbackDisabled", "heTrigMuBeamformingPartialFeedbackDisabled",
        "heTrigCqiFeedbackDisabled", "vhtSuBeamformeeDisabled",
        "vhtMuBeamformeeDisabled", "vhtMcs8Enabled",
    };
    static const char *const dcm_names[] = {"heDcmMaxConstellationTx", "heDcmMaxConstellationRx"};
    bool flags[9] = {0};
    uint32_t dcm[2] = {3, 3};
    bool dcm_present[2] = {false, false};
    bool ok = false;
    JSGCRef options_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *root = options;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        *value = JS_GetPropertyStr(ctx, *root, names[i]);
        if (JS_IsException(*value)) goto done;
        if (JS_IsUndefined(*value)) continue;
        if (!JS_IsBool(*value)) goto invalid;
#if !CONFIG_SOC_WIFI_HE_SUPPORT
        if (i < 6) { wifi_station_unsupported(ctx, names[i], operation); goto done; }
#endif
        /* In the pinned SDK the 5 GHz target implements 802.11ac. This only
         * admits explicit PHY configuration; it does not enable a protocol. */
#if !CONFIG_SOC_WIFI_SUPPORT_5G
        if (i >= 6) { wifi_station_unsupported(ctx, names[i], operation); goto done; }
#endif
        flags[i] = *value == JS_TRUE;
    }
    for (size_t i = 0; i < 2; ++i) {
        *value = JS_GetPropertyStr(ctx, *root, dcm_names[i]);
        if (JS_IsException(*value)) goto done;
        dcm_present[i] = !JS_IsUndefined(*value);
        if (!dcm_present[i]) continue;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 3, &dcm[i])) goto invalid;
#if !CONFIG_SOC_WIFI_HE_SUPPORT
        wifi_station_unsupported(ctx, dcm_names[i], operation); goto done;
#endif
    }
    if ((dcm_present[0] || dcm_present[1]) && !flags[0]) {
        JS_ThrowTypeError(ctx, "explicit DCM constellation requires heDcmSet:true");
        goto done;
    }
    /* Bitfields cannot be addressed through offsetof or bool pointers. Parse
     * into local scalars before assigning, preserving every reserved bit. */
    sta->he_dcm_set = flags[0];
    sta->he_dcm_max_constellation_tx = flags[0] ? dcm[0] : 0;
    sta->he_dcm_max_constellation_rx = flags[0] ? dcm[1] : 0;
    sta->he_mcs9_enabled = flags[1];
    sta->he_su_beamformee_disabled = flags[2];
    sta->he_trig_su_bmforming_feedback_disabled = flags[3];
    sta->he_trig_mu_bmforming_partial_feedback_disabled = flags[4];
    sta->he_trig_cqi_feedback_disabled = flags[5];
    sta->vht_su_beamformee_disabled = flags[6];
    sta->vht_mu_beamformee_disabled = flags[7];
    sta->vht_mcs8_enabled = flags[8];
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid wifi.connect PHY option");
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}

static bool wifi_parse_station_extensions(JSContext *ctx, JSValue options, wifi_sta_config_t *sta, const char *operation)
{
    static const char *const flag_names[] = {"rmEnabled", "btmEnabled", "mboEnabled", "ftEnabled", "oweEnabled",
                                           "transitionDisable", "disableWpa3CompatibleMode"};
    static const char *const pwe_names[] = {"hunting-and-pecking", "hash-to-element", "both"};
    static const wifi_sae_pwe_method_t pwe_values[] = {WPA3_SAE_PWE_HUNT_AND_PECK, WPA3_SAE_PWE_HASH_TO_ELEMENT, WPA3_SAE_PWE_BOTH};
    bool flag[7] = {0}, present[7] = {0};
    bool pwe_present = false, identifier_present = false;
    bool ok = false;
    JSGCRef options_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *root = options;
    uint32_t number;
    *value = JS_GetPropertyStr(ctx, *root, "listenInterval");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, UINT16_MAX, &number)) goto invalid;
        sta->listen_interval = number;
    }
    *value = JS_GetPropertyStr(ctx, *root, "failureRetryCount");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, UINT8_MAX, &number)) goto invalid;
        if (number != 0 && sta->scan_method != WIFI_ALL_CHANNEL_SCAN) goto conflict;
        sta->failure_retry_cnt = number;
    }
    *value = JS_GetPropertyStr(ctx, *root, "rssi5gAdjustment");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, UINT8_MAX, &number)) goto invalid;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
        wifi_station_unsupported(ctx, "rssi5gAdjustment", operation); goto done;
#endif
        sta->threshold.rssi_5g_adjustment = number;
    }
    for (size_t i = 0; i < sizeof(flag_names) / sizeof(flag_names[0]); ++i) {
        *value = JS_GetPropertyStr(ctx, *root, flag_names[i]);
        if (JS_IsException(*value)) goto done;
        present[i] = !JS_IsUndefined(*value);
        if (present[i]) {
            if (!JS_IsBool(*value)) goto invalid;
            flag[i] = *value == JS_TRUE;
        }
    }
    if (sta->threshold.authmode == WIFI_AUTH_OWE) {
        if (present[4] && !flag[4]) goto conflict;
        flag[4] = true;
    }
    if (flag[2]) {
        if ((present[0] && !flag[0]) || (present[1] && !flag[1])) goto conflict;
        flag[0] = true; flag[1] = true; /* SDK MBO dependency, made explicit. */
    }
#if !CONFIG_ESP_WIFI_11KV_SUPPORT || !CONFIG_ESP_WIFI_RRM_SUPPORT
    if (flag[0]) { wifi_station_unsupported(ctx, "rmEnabled", operation); goto done; }
#endif
#if !CONFIG_ESP_WIFI_11KV_SUPPORT || !CONFIG_ESP_WIFI_WNM_SUPPORT
    if (flag[1]) { wifi_station_unsupported(ctx, "btmEnabled", operation); goto done; }
#endif
#if !CONFIG_ESP_WIFI_MBO_SUPPORT
    if (flag[2]) { wifi_station_unsupported(ctx, "mboEnabled", operation); goto done; }
#endif
#if !CONFIG_ESP_WIFI_11R_SUPPORT
    if (flag[3]) { wifi_station_unsupported(ctx, "ftEnabled", operation); goto done; }
#endif
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA
    if (flag[4]) { wifi_station_unsupported(ctx, "oweEnabled", operation); goto done; }
#endif
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_SAE && !CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA
    if (flag[5]) { wifi_station_unsupported(ctx, "transitionDisable", operation); goto done; }
#endif
#if !CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
    if (flag[6]) { wifi_station_unsupported(ctx, "disableWpa3CompatibleMode", operation); goto done; }
#endif
    if ((flag[1] || flag[2]) && (sta->bssid_set || sta->channel != 0)) goto conflict;
    sta->rm_enabled = flag[0]; sta->btm_enabled = flag[1]; sta->mbo_enabled = flag[2];
    sta->ft_enabled = flag[3]; sta->owe_enabled = flag[4];
    sta->transition_disable = flag[5];
    sta->disable_wpa3_compatible_mode = flag[6];

    *value = JS_GetPropertyStr(ctx, *root, "saePwe");
    if (JS_IsException(*value)) goto done;
    pwe_present = !JS_IsUndefined(*value);
    if (pwe_present) {
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *value, pwe_names, 3, &index)) goto invalid;
        sta->sae_pwe_h2e = pwe_values[index];
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
        wifi_station_unsupported(ctx, "saePwe", operation); goto done;
#endif
#if !CONFIG_ESP_WIFI_ENABLE_SAE_H2E
        if (sta->sae_pwe_h2e != WPA3_SAE_PWE_HUNT_AND_PECK) { wifi_station_unsupported(ctx, "saePwe", operation); goto done; }
#endif
    }
    *value = JS_GetPropertyStr(ctx, *root, "saeH2eIdentifier");
    if (JS_IsException(*value)) goto done;
    identifier_present = !JS_IsUndefined(*value);
    if (identifier_present) {
        JSCStringBuf buffer; size_t length;
        if (!JS_IsString(ctx, *value)) goto invalid;
        const char *text = JS_ToCStringLen(ctx, &length, *value, &buffer);
        if (text == NULL) goto done;
        if (length == 0 || length > sizeof(sta->sae_h2e_identifier) || memchr(text, 0, length) != NULL) goto invalid;
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_SAE || !CONFIG_ESP_WIFI_ENABLE_SAE_H2E
        wifi_station_unsupported(ctx, "saeH2eIdentifier", operation); goto done;
#endif
        if (pwe_present && sta->sae_pwe_h2e == WPA3_SAE_PWE_HUNT_AND_PECK) goto conflict;
        if (!pwe_present) sta->sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        memcpy(sta->sae_h2e_identifier, text, length);
    }
    if ((pwe_present || identifier_present) &&
        (sta->password[0] == 0 || flag[4] ||
         strnlen((const char *)sta->password, sizeof(sta->password)) == sizeof(sta->password))) goto conflict;
    *value = JS_GetPropertyStr(ctx, *root, "saePkMode");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (wifi_string_equals(ctx, *value, "automatic")) sta->sae_pk_mode = WPA3_SAE_PK_MODE_AUTOMATIC;
        else if (wifi_string_equals(ctx, *value, "only")) sta->sae_pk_mode = WPA3_SAE_PK_MODE_ONLY;
        else if (wifi_string_equals(ctx, *value, "disabled")) sta->sae_pk_mode = WPA3_SAE_PK_MODE_DISABLED;
        else goto invalid;
#if !CONFIG_ESP_WIFI_ENABLE_SAE_PK
        if (sta->sae_pk_mode != WPA3_SAE_PK_MODE_DISABLED) {
            wifi_station_unsupported(ctx, "saePkMode", operation); goto done;
        }
#endif
    }
    if (sta->sae_pk_mode == WPA3_SAE_PK_MODE_ONLY) {
        size_t password_length = strnlen((const char *)sta->password, sizeof(sta->password));
        if (password_length == 0 || password_length == sizeof(sta->password) || flag[4] ||
            (pwe_present && sta->sae_pwe_h2e == WPA3_SAE_PWE_HUNT_AND_PECK)) goto conflict;
        *value = JS_GetPropertyStr(ctx, *root, "minimumAuthMode");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value) && sta->threshold.authmode != WIFI_AUTH_WPA3_PSK) goto conflict;
        *value = JS_GetPropertyStr(ctx, *root, "pmf");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value) && !sta->pmf_cfg.required) goto conflict;
        /* PK-only is enforced on SAE handshakes by the native supplicant.
         * Exclude the WPA2 path, which would otherwise bypass that check. */
        sta->threshold.authmode = WIFI_AUTH_WPA3_PSK;
        sta->pmf_cfg.capable = true;
        sta->pmf_cfg.required = true;
        if (!pwe_present) sta->sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    }
    if (flag[4]) {
        if (sta->password[0] != 0) goto conflict;
        *value = JS_GetPropertyStr(ctx, *root, "minimumAuthMode");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value) && sta->threshold.authmode != WIFI_AUTH_OWE) goto conflict;
        *value = JS_GetPropertyStr(ctx, *root, "pmf");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value) && !sta->pmf_cfg.required) goto conflict;
        /* OWE selection must not fall back to an unencrypted open network. */
        sta->threshold.authmode = WIFI_AUTH_OWE;
        sta->pmf_cfg.capable = true;
        sta->pmf_cfg.required = true;
    }
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    if (sta->threshold.authmode == WIFI_AUTH_WPA3_PSK || sta->threshold.authmode == WIFI_AUTH_WPA2_WPA3_PSK) {
        wifi_station_unsupported(ctx, "minimumAuthMode", operation); goto done;
    }
#endif
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA
    if (sta->threshold.authmode == WIFI_AUTH_OWE) { wifi_station_unsupported(ctx, "minimumAuthMode", operation); goto done; }
#endif
#if !CONFIG_ESP_WIFI_WAPI_PSK
    if (sta->threshold.authmode == WIFI_AUTH_WAPI_PSK) { wifi_station_unsupported(ctx, "minimumAuthMode", operation); goto done; }
#endif
    ok = wifi_parse_station_phy(ctx, *root, sta, operation);
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid wifi.connect station option");
    goto done;
conflict:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "conflicting wifi.connect station options");
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}

bool esp32_mquickjs_wifi_parse_station_config_for_operation(JSContext *ctx, int argc,
    JSGCRef *argv, wifi_config_t *config, uint32_t *timeout_ms, const char *operation)
{
    static const char *const allowed[] = {
        "password", "timeoutMs", "bssid", "channel", "scanMethod",
        "sortMethod", "minimumRssi", "minimumAuthMode", "pmf",
        "listenInterval", "failureRetryCount", "rmEnabled", "btmEnabled",
        "mboEnabled", "ftEnabled", "oweEnabled", "saePwe", "saeH2eIdentifier",
        "rssi5gAdjustment",
        "transitionDisable", "disableWpa3CompatibleMode", "saePkMode",
        "heDcmSet", "heDcmMaxConstellationTx", "heDcmMaxConstellationRx", "heMcs9Enabled",
        "heSuBeamformeeDisabled", "heTrigSuBeamformingFeedbackDisabled",
        "heTrigMuBeamformingPartialFeedbackDisabled", "heTrigCqiFeedbackDisabled",
        "vhtSuBeamformeeDisabled", "vhtMuBeamformeeDisabled", "vhtMcs8Enabled",
    };
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    JSCStringBuf ssid_buf;
    JSCStringBuf password_buf;
    const char *ssid;
    const char *password = "";
    size_t ssid_len = 0;
    size_t password_len = 0;
    int32_t integer;
    bool result = false;

    if (argc < 1 || argc > 2 || !JS_IsString(ctx, argv[0].val) ||
        (argc == 2 && !wifi_is_object(ctx, argv[1].val))) {
        JS_ThrowTypeError(
            ctx,
            "wifi.connect(ssid, options?) expects a string SSID and optional options object");
        goto done;
    }
    if (argc == 2 &&
        !wifi_validate_option_keys(ctx, argv[1].val, "wifi.connect()",
                                   allowed, sizeof(allowed) / sizeof(allowed[0]))) {
        goto done;
    }
    memset(config, 0, sizeof(*config));
    *timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    config->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config->sta.threshold.rssi = -127;
    config->sta.pmf_cfg.capable = true;

    ssid = JS_ToCStringLen(ctx, &ssid_len, argv[0].val, &ssid_buf);
    if (ssid == NULL || ssid_len == 0 || ssid_len > ESP32_MQUICKJS_WIFI_SSID_MAX_LEN ||
        memchr(ssid, 0, ssid_len) != NULL) {
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx,
                          "wifi.connect(ssid, ...) expects 1..%d bytes without NUL",
                          ESP32_MQUICKJS_WIFI_SSID_MAX_LEN);
        goto done;
    }
    memcpy(config->sta.ssid, ssid, ssid_len);
    config->sta.threshold.authmode = WIFI_AUTH_OPEN;
    if (argc == 1) {
        result = true;
        goto done;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "password");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        password = JS_IsString(ctx, *property)
                       ? JS_ToCStringLen(ctx, &password_len, *property,
                                         &password_buf)
                       : NULL;
        if (password == NULL ||
            password_len > ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN ||
            memchr(password, 0, password_len) != NULL) {
            if (!JS_HasException(ctx)) JS_ThrowTypeError(
                ctx, "wifi.connect({ password }) expects at most %d bytes without NUL",
                ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN);
            goto done;
        }
        if (password_len == sizeof(config->sta.password)) {
            for (size_t i = 0; i < password_len; ++i) {
                char c = password[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                    JS_ThrowTypeError(ctx, "wifi.connect 64-byte password must be a hexadecimal PSK");
                    goto done;
                }
            }
        }
        memcpy(config->sta.password, password, password_len);
    }
    config->sta.threshold.authmode =
        password_len > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    *property = JS_GetPropertyStr(ctx, argv[1].val, "timeoutMs");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!wifi_to_integer(ctx, *property, 1, 60000, &integer)) {
            if (!JS_HasException(ctx)) JS_ThrowRangeError(
                ctx, "wifi.connect({ timeoutMs }) expects 1..60000");
            goto done;
        }
        *timeout_ms = (uint32_t)integer;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "bssid");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!wifi_parse_bssid(ctx, *property,
                              config->sta.bssid) || (config->sta.bssid[0] & 1U) != 0 ||
            memcmp(config->sta.bssid, (uint8_t[6]){0}, 6) == 0) {
            if (!JS_HasException(ctx)) JS_ThrowTypeError(
                ctx, "wifi.connect({ bssid }) expects xx:xx:xx:xx:xx:xx");
            goto done;
        }
        config->sta.bssid_set = true;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "channel");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!wifi_to_integer(ctx, *property, 0, 177, &integer) ||
            (integer > 13 &&
#if CONFIG_SOC_WIFI_SUPPORT_5G
             esp32_mquickjs_wifi_radio_5ghz_channel_bit((uint8_t)integer) == 0
#else
             true
#endif
            )) {
            if (!JS_HasException(ctx)) JS_ThrowRangeError(
                ctx, "wifi.connect({ channel }) expects 0 or a target-supported channel hint");
            goto done;
        }
        config->sta.channel = (uint8_t)integer;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "scanMethod");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (wifi_string_equals(ctx, *property, "fast")) {
            config->sta.scan_method = WIFI_FAST_SCAN;
        } else if (wifi_string_equals(ctx, *property, "all-channel")) {
            config->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        } else {
            if (!JS_HasException(ctx)) JS_ThrowRangeError(
                ctx, "wifi.connect({ scanMethod }) expects fast or all-channel");
            goto done;
        }
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "sortMethod");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (wifi_string_equals(ctx, *property, "signal")) {
            config->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        } else if (wifi_string_equals(ctx, *property, "security")) {
            config->sta.sort_method = WIFI_CONNECT_AP_BY_SECURITY;
        } else {
            if (!JS_HasException(ctx)) JS_ThrowRangeError(
                ctx, "wifi.connect({ sortMethod }) expects signal or security");
            goto done;
        }
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "minimumRssi");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!wifi_to_integer(ctx, *property, -127, 0, &integer)) {
            if (!JS_HasException(ctx)) JS_ThrowRangeError(
                ctx, "wifi.connect({ minimumRssi }) expects -127..0");
            goto done;
        }
        config->sta.threshold.rssi = (int8_t)integer;
    }

    *property = JS_GetPropertyStr(ctx, argv[1].val, "minimumAuthMode");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property) &&
        !wifi_parse_auth_mode(
            ctx, *property, &config->sta.threshold.authmode)) {
        if (!JS_HasException(ctx)) JS_ThrowRangeError(
            ctx, "wifi.connect({ minimumAuthMode }) is not a supported personal auth mode");
        goto done;
    }

    config->sta.pmf_cfg.required = config->sta.threshold.authmode == WIFI_AUTH_WPA3_PSK ||
        config->sta.threshold.authmode == WIFI_AUTH_OWE;
    *property = JS_GetPropertyStr(ctx, argv[1].val, "pmf");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (wifi_string_equals(ctx, *property, "disabled")) {
            config->sta.pmf_cfg.capable = false;
            config->sta.pmf_cfg.required = false;
        } else if (wifi_string_equals(ctx, *property, "optional")) {
            config->sta.pmf_cfg.capable = true;
            config->sta.pmf_cfg.required = false;
        } else if (wifi_string_equals(ctx, *property, "required")) {
            config->sta.pmf_cfg.capable = true;
            config->sta.pmf_cfg.required = true;
        } else {
            if (!JS_HasException(ctx)) JS_ThrowRangeError(
                ctx, "%s({ pmf }) expects disabled, optional or required", operation);
            goto done;
        }
    }
    result = wifi_parse_station_extensions(ctx, argv[1].val, &config->sta, operation);
    if (result && (config->sta.threshold.authmode == WIFI_AUTH_WPA3_PSK ||
        config->sta.threshold.authmode == WIFI_AUTH_OWE) && !config->sta.pmf_cfg.required) {
        JS_ThrowTypeError(ctx, "Station authentication requires PMF");
        result = false;
    }
    if (result && !config->sta.pmf_cfg.capable &&
        !esp32_mquickjs_wifi_radio_pmf_disable_allowed(WIFI_IF_STA, config)) {
        JS_ThrowTypeError(ctx, "PMF disabled conflicts with Station authentication or WPA3 compatible policy");
        result = false;
    }

done:
    if (!result) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    esp32_mquickjs_wireless_secure_zero(&ssid_buf, sizeof(ssid_buf));
    esp32_mquickjs_wireless_secure_zero(&password_buf, sizeof(password_buf));
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

bool esp32_mquickjs_wifi_parse_station_config(JSContext *ctx, int argc,
    JSGCRef *argv, wifi_config_t *config, uint32_t *timeout_ms)
{
    return esp32_mquickjs_wifi_parse_station_config_for_operation(ctx, argc, argv,
        config, timeout_ms, "wifi.connect");
}

static bool wifi_future_parse_connect(JSContext *ctx, int argc, JSGCRef *argv,
                                      esp32_mquickjs_future_driver_state_t *state)
{
    return esp32_mquickjs_wifi_parse_connect_config(ctx, argc, argv,
        &state->connect_config, &state->timeout_ms);
}

static bool wifi_scan_parse_channels(JSContext *ctx, JSValue input, wifi_scan_channel_bitmap_t *bitmap)
{
    static const char *const keys[] = {"ghz2", "ghz5"};
    JSGCRef object_ref, array_ref, item_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *object = input;
    uint32_t bits[2] = {1U, 1U}; /* Omitted band is bypassed, never scanned in full. */
    bool ok = false;
    if (!wifi_validate_option_keys(ctx, *object, "wifi.scan channels", keys, 2)) goto done;
    for (size_t band = 0; band < 2; ++band) {
        *array = JS_GetPropertyStr(ctx, *object, keys[band]);
        if (JS_IsException(*array)) goto done;
        if (JS_IsUndefined(*array)) continue;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
        if (band == 1) {
            JS_ThrowTypeError(ctx, "wifi.scan channels.ghz5 is unsupported on this target");
            goto done;
        }
#endif
        if (!JS_IsArray(ctx, *array)) goto invalid;
        *item = JS_GetPropertyStr(ctx, *array, "length");
        if (JS_IsException(*item)) goto done;
        int32_t length;
        if (!wifi_to_integer(ctx, *item, 1, band == 0 ? 14 : 28, &length)) goto invalid;
        bits[band] = 0;
        for (int32_t i = 0; i < length; ++i) {
            *item = JS_GetPropertyUint32(ctx, *array, i);
            if (JS_IsException(*item)) goto done;
            int32_t channel;
            if (!wifi_to_integer(ctx, *item, 1, band == 0 ? 14 : 177, &channel)) goto invalid;
            uint32_t bit = band == 0 ? 1UL << channel :
                esp32_mquickjs_wifi_radio_5ghz_channel_bit((uint8_t)channel);
            if (bit == 0 || (bits[band] & bit) != 0) goto invalid;
            bits[band] |= bit;
        }
    }
    if (bits[0] == 1U && bits[1] == 1U) goto invalid;
    bitmap->ghz_2_channels = bits[0];
    bitmap->ghz_5_channels = bits[1];
    ok = true;
    goto done;
invalid:
    JS_ThrowTypeError(ctx, "wifi.scan channels requires nonempty arrays of distinct valid channel numbers");
done:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &object_ref);
    return ok;
}

static bool wifi_scan_future_prepare(JSContext *ctx,
                                     JSGCRef *this_ref,
                                     int argc,
                                     JSGCRef *argv,
                                     esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {
        "channel", "showHidden", "mode", "activeMinMs", "activeMaxMs", "passiveMs", "timeoutMs",
        "ssid", "bssid", "homeChannelDwellMs", "coexistenceBackgroundScan", "maxRecords",
        "channels",
    };
    esp32_mquickjs_future_driver_state_t *state;
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {.min = WIFI_ACTIVE_SCAN_MIN_DEFAULT_TIME, .max = WIFI_ACTIVE_SCAN_MAX_DEFAULT_TIME},
            .passive = WIFI_PASSIVE_SCAN_DEFAULT_TIME,
        },
        .home_chan_dwell_time = 30,
    };
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    int32_t integer;
    bool active_time_supplied = false;
    bool passive_time_supplied = false;
    bool result = false;

    (void)this_ref;
    if (out_state == NULL || argc < 0 || argc > 1 ||
        (argc == 1 && !wifi_is_object(ctx, argv[0].val))) {
        JS_ThrowTypeError(ctx, "wifi.scan(options?) expects an options object");
        goto done;
    }
    if (argc == 1 &&
        !wifi_validate_option_keys(ctx, argv[0].val, "wifi.scan()",
                                   allowed, sizeof(allowed) / sizeof(*allowed))) {
        goto done;
    }
    state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    state->kind = WIFI_FUTURE_SCAN;
    state->timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    state->scan_max_records = ESP32_MQUICKJS_WIFI_MAX_SCAN_RECORDS;
    if (argc == 1) {
        *property = JS_GetPropertyStr(ctx, argv[0].val, "ssid");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            JSCStringBuf buffer;
            size_t length;
            const char *text;
            if (!JS_IsString(ctx, *property)) {
                JS_ThrowTypeError(ctx, "wifi.scan({ ssid }) expects a string");
                goto fail;
            }
            text = JS_ToCStringLen(ctx, &length, *property, &buffer);
            if (text == NULL) goto fail;
            if (length == 0 || length > 32 || memchr(text, 0, length) != NULL) {
                JS_ThrowRangeError(ctx, "wifi.scan({ ssid }) expects 1..32 UTF-8 bytes without NUL");
                goto fail;
            }
            memcpy(state->scan_ssid, text, length);
            scan_config.ssid = state->scan_ssid;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "bssid");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!wifi_parse_bssid(ctx, *property, state->scan_bssid) ||
                (state->scan_bssid[0] & 1U) != 0U ||
                memcmp(state->scan_bssid, "\0\0\0\0\0\0", 6) == 0) {
                JS_ThrowTypeError(ctx, "wifi.scan({ bssid }) expects a nonzero unicast MAC (xx:xx:xx:xx:xx:xx)");
                goto fail;
            }
            scan_config.bssid = state->scan_bssid;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "homeChannelDwellMs");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 30, 150, &integer)) {
                JS_ThrowRangeError(ctx, "wifi.scan({ homeChannelDwellMs }) expects 30..150");
                goto fail;
            }
            scan_config.home_chan_dwell_time = (uint8_t)integer;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "coexistenceBackgroundScan");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!JS_IsBool(*property)) {
                JS_ThrowTypeError(ctx, "wifi.scan({ coexistenceBackgroundScan }) expects a boolean");
                goto fail;
            }
            scan_config.coex_background_scan = *property == JS_TRUE;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "maxRecords");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 1, ESP32_MQUICKJS_WIFI_MAX_SCAN_RECORDS, &integer)) {
                JS_ThrowRangeError(ctx, "wifi.scan({ maxRecords }) expects 1..32");
                goto fail;
            }
            state->scan_max_records = (uint16_t)integer;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "channel");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property) && !wifi_string_equals(ctx, *property, "all")) {
            if (!wifi_to_integer(ctx, *property, 1, 255, &integer)) {
                JS_ThrowRangeError(
                    ctx, "wifi.scan({ channel }) expects all or 1..255");
                goto fail;
            }
            scan_config.channel = (uint8_t)integer;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "channels");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (scan_config.channel != 0) {
                JS_ThrowTypeError(ctx, "wifi.scan channel number and channels are mutually exclusive");
                goto fail;
            }
            if (!wifi_scan_parse_channels(ctx, *property, &scan_config.channel_bitmap)) goto fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "showHidden");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            if (!JS_IsBool(*property)) {
                JS_ThrowTypeError(
                    ctx, "wifi.scan({ showHidden }) expects a boolean");
                goto fail;
            }
            scan_config.show_hidden = *property == JS_TRUE;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "mode");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            if (wifi_string_equals(ctx, *property, "active")) {
                scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            } else if (wifi_string_equals(ctx, *property, "passive")) {
                scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
            } else {
                JS_ThrowTypeError(ctx, "wifi.scan({ mode }) expects active or passive");
                goto fail;
            }
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "activeMinMs");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 0, 1500, &integer)) {
                JS_ThrowRangeError(ctx, "wifi.scan({ activeMinMs }) expects 0..1500");
                goto fail;
            }
            scan_config.scan_time.active.min = (uint32_t)integer;
            active_time_supplied = true;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "activeMaxMs");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 1, 1500, &integer)) {
                JS_ThrowRangeError(ctx, "wifi.scan({ activeMaxMs }) expects 1..1500");
                goto fail;
            }
            scan_config.scan_time.active.max = (uint32_t)integer;
            active_time_supplied = true;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "passiveMs");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 1, 1500, &integer)) {
                JS_ThrowRangeError(ctx, "wifi.scan({ passiveMs }) expects 1..1500");
                goto fail;
            }
            scan_config.scan_time.passive = (uint32_t)integer;
            passive_time_supplied = true;
        }
        if (scan_config.scan_time.active.min > scan_config.scan_time.active.max) {
            JS_ThrowRangeError(ctx, "wifi.scan activeMinMs must not exceed activeMaxMs");
            goto fail;
        }
        if ((scan_config.scan_type == WIFI_SCAN_TYPE_ACTIVE && passive_time_supplied) ||
            (scan_config.scan_type == WIFI_SCAN_TYPE_PASSIVE && active_time_supplied)) {
            JS_ThrowTypeError(ctx, "wifi.scan timing fields must match mode");
            goto fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
        if (JS_IsException(*property)) {
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            if (!wifi_to_integer(ctx, *property, 1, 60000, &integer)) {
                JS_ThrowRangeError(
                    ctx, "wifi.scan({ timeoutMs }) expects 1..60000");
                goto fail;
            }
            state->timeout_ms = (uint32_t)integer;
        }
    }
    state->scan_config = scan_config;
    *out_state = state;
    result = true;
    goto done;

fail:
    esp32_mquickjs_memory_payload_free(state);
done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool wifi_connect_future_prepare(JSContext *ctx,
                                        JSGCRef *this_ref,
                                        int argc,
                                        JSGCRef *argv,
                                        esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    if (out_state == NULL) {
        return false;
    }
    state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->kind = WIFI_FUTURE_CONNECT;
    if (!wifi_future_parse_connect(ctx, argc, argv, state)) {
        esp32_mquickjs_wireless_secure_zero(
            &state->connect_config, sizeof(state->connect_config));
        esp32_mquickjs_memory_payload_free(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool wifi_disconnect_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    (void)this_ref;
    if (out_state == NULL || argc < 0 || argc > 1) {
        JS_ThrowTypeError(
            ctx, "wifi.disconnect(timeoutMs?) expects at most one timeout");
        return false;
    }
    state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->kind = WIFI_FUTURE_DISCONNECT;
    state->timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    if (argc == 1 &&
        (!JS_IsNumber(ctx, argv[0].val) ||
         esp32_mquickjs_wifi_value_to_timeout_ms(
             ctx, argv[0].val, ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS,
             &state->timeout_ms) != 0)) {
        esp32_mquickjs_memory_payload_free(state);
        JS_ThrowTypeError(
            ctx, "wifi.disconnect(timeoutMs) expects a non-negative integer");
        return false;
    }
    *out_state = state;
    return true;
}

static bool wifi_future_start(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              esp32_mquickjs_future_token_t token,
                              esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_state_t *wifi = esp32_mquickjs_wifi_state();
    esp_err_t err;
    bool disconnect_pending = false;

    if (state == NULL) {
        JS_ThrowInternalError(ctx, "Wi-Fi Future lost its driver state");
        return false;
    }
    if (state->kind == WIFI_FUTURE_DISCONNECT &&
        (!wifi->initialized || !wifi->started)) {
        state->connect_kind =
            ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED;
        state->completed = true;
        state->started = true;
        return true;
    }
    if (state->kind == WIFI_FUTURE_CONNECT) {
        /* Reject an exhausted identity before preparation can disconnect an
         * established Station. Registration rechecks after the handoff. */
        esp32_mquickjs_wifi_lock();
        bool exhausted = wifi->connect_generation == UINT32_MAX;
        esp32_mquickjs_wifi_unlock();
        if (exhausted) {
            esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_OPERATION_BUSY",
                "wifi.connect", ESP_ERR_INVALID_STATE, -1, UINT32_MAX);
            return false;
        }
    }
    /* A disabled-PMF connect must configure while stopped. This may retire
     * and recreate the helper, so do it before publishing the Future token. */
    err = state->kind == WIFI_FUTURE_CONNECT
        ? esp32_mquickjs_wifi_prepare_connect(&state->connect_config)
        : esp32_mquickjs_wifi_ensure_started();
    if (err != ESP_OK) {
        if (state->kind == WIFI_FUTURE_SCAN) {
            esp32_mquickjs_wifi_throw_scan_error(ctx, err);
        } else {
            esp32_mquickjs_wifi_throw_connect_error(ctx, err);
        }
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    (void)esp32_mquickjs_wifi_drain_scan();

    esp32_mquickjs_wifi_lock();
    if (state->kind == WIFI_FUTURE_SCAN) {
        if (wifi->scan_in_progress || wifi->scan_future_registered ||
            wifi->scan_draining || wifi->scan_results_pending ||
            wifi->scan_stop_active || wifi->connect_in_progress || wifi->connect_draining ||
            esp32_mquickjs_wifi_connection_reserved_locked() || wifi->scan_generation == UINT32_MAX) {
            esp32_mquickjs_wifi_unlock();
            esp32_mquickjs_wifi_throw_operation_error(
                ctx, "WIFI_SCAN_BUSY", "wifi.scan", ESP_ERR_INVALID_STATE,
                -1, UINT32_MAX);
            return false;
        }
        wifi->scan_generation++;
        state->generation = wifi->scan_generation;
        wifi->scan_future_registered = true;
        wifi->scan_future_token = token;
    } else {
        if (esp32_mquickjs_wifi_connection_reserved_locked() || wifi->connect_generation == UINT32_MAX ||
            (state->kind == WIFI_FUTURE_CONNECT &&
             (wifi->connect_in_progress || wifi->scan_in_progress || wifi->scan_future_registered ||
              wifi->scan_draining || wifi->scan_results_pending || wifi->scan_stop_active))) {
            esp32_mquickjs_wifi_unlock();
            esp32_mquickjs_wifi_throw_operation_error(
                ctx, "WIFI_OPERATION_BUSY",
                wifi_future_operation_name(state->kind),
                ESP_ERR_INVALID_STATE, -1, UINT32_MAX);
            return false;
        }
        wifi->connect_generation++;
        state->generation = wifi->connect_generation;
        wifi->connect_future_registered = true;
        wifi->connect_future_token = token;
        wifi->connection_future_operation =
            state->kind == WIFI_FUTURE_CONNECT
                ? ESP32_MQUICKJS_WIFI_OPERATION_CONNECT
                : ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    }
    esp32_mquickjs_wifi_unlock();

    if (state->kind == WIFI_FUTURE_SCAN) {
        if (wifi->scan_queue != NULL) {
            xQueueReset(wifi->scan_queue);
        }
        err = esp32_mquickjs_wifi_start_scan(&state->scan_config, state->generation);
        if (err != ESP_OK) {
            esp32_mquickjs_wifi_clear_scan_future();
            esp32_mquickjs_wifi_throw_scan_error(ctx, err);
            return false;
        }
    } else if (state->kind == WIFI_FUTURE_CONNECT) {
        state->connect_start_us = esp_timer_get_time();
        err = esp32_mquickjs_wifi_start_connect(&state->connect_config,
                                                state->timeout_ms);
        if (err != ESP_OK) {
            esp32_mquickjs_wifi_clear_connect_future();
            esp32_mquickjs_wifi_throw_connect_error(ctx, err);
            return false;
        }
    } else {
        if (wifi->connect_queue != NULL) {
            xQueueReset(wifi->connect_queue);
        }
        err = esp32_mquickjs_wifi_start_disconnect(&disconnect_pending);
        if (err != ESP_OK) {
            esp32_mquickjs_wifi_clear_connect_future();
            esp32_mquickjs_wifi_throw_operation_error(
                ctx, "WIFI_DISCONNECT_FAILED", "wifi.disconnect", err,
                -1, UINT32_MAX);
            return false;
        }
        if (!disconnect_pending) {
            state->connect_kind =
                ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED;
            state->completed = true;
        }
    }
    state->started = true;
    return true;
}

static esp32_mquickjs_future_poll_t wifi_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_state_t *wifi = esp32_mquickjs_wifi_state();

    if (state == NULL) {
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->completed) {
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->kind == WIFI_FUTURE_SCAN) {
        esp32_mquickjs_wifi_scan_event_t event;

        while (wifi->scan_queue != NULL && xQueueReceive(wifi->scan_queue, &event, 0) == pdTRUE) {
            if (event.generation == state->generation) {
                state->scan_status = event.status;
                state->completed = true;
                break;
            }
        }
    } else {
        esp32_mquickjs_wifi_connect_event_t event;

        while (wifi->connect_queue != NULL &&
               xQueueReceive(wifi->connect_queue, &event, 0) == pdTRUE) {
            if (event.generation == state->generation) {
                state->connect_kind = event.kind;
                state->connect_reason = event.reason;
                state->connect_completed_us = event.completed_us;
                state->connect_link = event.link;
                state->completed = true;
                break;
            }
        }
    }
    return state->completed
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue wifi_future_finish(JSContext *ctx,
                                  esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cancel_requested) {
        return esp32_mquickjs_wifi_throw_operation_error(
            ctx, "WIFI_CANCELLED",
            state != NULL ? wifi_future_operation_name(state->kind)
                          : "wifi",
            ESP_ERR_INVALID_STATE, -1, UINT32_MAX);
    }
    if (state->kind == WIFI_FUTURE_SCAN) {
        if (state->scan_status != 0) {
            (void)esp32_mquickjs_wifi_cancel_scan(state->generation);
            return esp32_mquickjs_wifi_throw_operation_error(
                ctx, "WIFI_SCAN_FAILED", "wifi.scan", ESP_FAIL, -1,
                state->scan_status);
        }
        JSValue result = esp32_mquickjs_wifi_make_scan_results_array(ctx, state->scan_max_records);
        esp_err_t err = esp32_mquickjs_wifi_cancel_scan(state->generation);
        if (JS_IsException(result)) return result;
        if (err != ESP_OK) return esp32_mquickjs_wifi_throw_scan_error(ctx, err);
        return result;
    }
    esp32_mquickjs_wifi_clear_connect_future();
    if (state->kind == WIFI_FUTURE_DISCONNECT) {
        if (state->connect_kind ==
            ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED) {
            return esp32_mquickjs_wifi_make_status_object(ctx);
        }
        return esp32_mquickjs_wifi_throw_operation_error(
            ctx, "WIFI_DISCONNECT_FAILED", "wifi.disconnect",
            ESP_ERR_INVALID_STATE, state->connect_reason, UINT32_MAX);
    }
    if (state->connect_kind == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS) {
        double elapsed_ms = state->connect_completed_us >= state->connect_start_us
            ? (double)(state->connect_completed_us - state->connect_start_us) / 1000.0 : 0;
        return esp32_mquickjs_wifi_make_connect_result(ctx, &state->connect_link, elapsed_ms);
    }
    if (state->connect_kind == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT) {
        return esp32_mquickjs_wifi_throw_operation_error(
            ctx, "WIFI_CONNECT_TIMEOUT", "wifi.connect", ESP_ERR_TIMEOUT,
            state->connect_reason, UINT32_MAX);
    }
    return esp32_mquickjs_wifi_throw_operation_error(
        ctx, "WIFI_CONNECT_FAILED", "wifi.connect", ESP_FAIL,
        state->connect_reason, UINT32_MAX);
}

static esp32_mquickjs_cancel_result_t wifi_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancel_requested) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancel_requested = true;
    if (state->kind == WIFI_FUTURE_DISCONNECT && state->started) {
        state->cancel_requested = false;
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    if (state->kind == WIFI_FUTURE_SCAN) {
        if (state->started)
            (void)esp32_mquickjs_wifi_cancel_scan(state->generation);
    } else if (state->started) {
        (void)esp32_mquickjs_wifi_cancel_connect(state->generation);
    }
    state->completed = true;
    if (state->runtime != NULL)
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    return ESP32_MQUICKJS_CANCELLED;
}

static void wifi_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->started && state->kind == WIFI_FUTURE_SCAN) {
        /* A completed-but-unread scan still owns the driver's AP list. */
        (void)esp32_mquickjs_wifi_cancel_scan(state->generation);
    } else if (state->started && !state->completed) {
        if (state->kind == WIFI_FUTURE_DISCONNECT) {
            /*
             * esp_wifi_disconnect() cannot be rolled back once submitted.  The
             * Future may still be destroyed during runtime teardown, though,
             * so detach its generation/token before releasing the driver
             * state.  A later STA_DISCONNECTED event will then update Wi-Fi
             * status without waking a stale Future token.
             */
            esp32_mquickjs_wifi_clear_connect_future();
        } else {
            (void)wifi_future_cancel(state);
        }
    }
    esp32_mquickjs_wireless_secure_zero(
        &state->connect_config, sizeof(state->connect_config));
    esp32_mquickjs_memory_payload_free(state);
}

static uint32_t wifi_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return 0;
    }
    return state->timeout_ms;
}

static const esp32_mquickjs_future_driver_t s_wifi_scan_future_driver = {
    .memory_owner = "wireless.future", .capture = wifi_scan_future_prepare,
    .start = wifi_future_start,
    .poll = wifi_future_poll,
    .finish = wifi_future_finish,
    .cancel = wifi_future_cancel,
    .destroy = wifi_future_destroy,
    .timeout_ms = wifi_future_timeout_ms,
};

static const esp32_mquickjs_future_driver_t s_wifi_connect_future_driver = {
    .memory_owner = "wireless.future", .capture = wifi_connect_future_prepare,
    .start = wifi_future_start,
    .poll = wifi_future_poll,
    .finish = wifi_future_finish,
    .cancel = wifi_future_cancel,
    .destroy = wifi_future_destroy,
    .timeout_ms = wifi_future_timeout_ms,
};

static const esp32_mquickjs_future_driver_t s_wifi_disconnect_future_driver = {
    .memory_owner = "wireless.future", .capture = wifi_disconnect_future_prepare,
    .start = wifi_future_start,
    .poll = wifi_future_poll,
    .finish = wifi_future_finish,
    .cancel = wifi_future_cancel,
    .destroy = wifi_future_destroy,
    .timeout_ms = wifi_future_timeout_ms,
};

bool esp32_mquickjs_init_wifi_future_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref;
    JSGCRef wifi_ref;
    JSGCRef scan_ref;
    JSGCRef connect_ref;
    JSGCRef disconnect_ref;
    JSValue *global;
    JSValue *wifi;
    JSValue *scan;
    JSValue *connect;
    JSValue *disconnect;
    bool result = false;

    global = JS_PushGCRef(ctx, &global_ref);
    wifi = JS_PushGCRef(ctx, &wifi_ref);
    scan = JS_PushGCRef(ctx, &scan_ref);
    connect = JS_PushGCRef(ctx, &connect_ref);
    disconnect = JS_PushGCRef(ctx, &disconnect_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_GetPropertyStr(ctx, *global, "wifi");
    *scan = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "scan");
    *connect = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "connect");
    *disconnect = JS_IsException(*wifi)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "disconnect");
    if (!JS_IsException(*global) && !JS_IsException(*wifi) &&
        !JS_IsException(*scan) && !JS_IsException(*connect) &&
        !JS_IsException(*disconnect) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *scan,
                                              &s_wifi_scan_future_driver) &&
        esp32_mquickjs_future_register_driver(ctx,
                                              runtime,
                                              *connect,
                                              &s_wifi_connect_future_driver) &&
        esp32_mquickjs_future_register_driver(
            ctx, runtime, *disconnect,
            &s_wifi_disconnect_future_driver)) {
        result = true;
    } else if (!JS_IsException(*scan) && !JS_IsException(*connect) &&
               !JS_IsException(*disconnect)) {
        JS_ThrowInternalError(ctx, "failed to register Wi-Fi Future drivers");
    }
    JS_PopGCRef(ctx, &disconnect_ref);
    JS_PopGCRef(ctx, &connect_ref);
    JS_PopGCRef(ctx, &scan_ref);
    JS_PopGCRef(ctx, &wifi_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

#endif
