#include "esp32_mquickjs_wifi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_core.h"
#include "esp_system.h"
#include "esp32_mquickjs_wifi_config_fields.h"
#include <string.h>

static JSValue wifi_capability_strings(JSContext *ctx, const char *const *values, size_t count)
{
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    for (size_t i = 0; i < count; ++i) {
        *item = JS_NewString(ctx, values[i]);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array, i, *item))) goto fail;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_station_capabilities(JSContext *ctx)
{
    static const struct { const char *name; bool available; } flags[] = {
        {"binarySsid", true}, {"driver", true}, {"listenInterval", true}, {"failureRetryCount", true},
#if CONFIG_SOC_WIFI_SUPPORT_5G
        {"rssi5gAdjustment", true},
#else
        {"rssi5gAdjustment", false},
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT
        {"he", true},
#else
        {"he", false},
#endif
#if CONFIG_SOC_WIFI_SUPPORT_5G
        {"vht", true},
#else
        {"vht", false},
#endif
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE || CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA
        {"transitionDisable", true},
#else
        {"transitionDisable", false},
#endif
#if CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
        {"disableWpa3CompatibleMode", true},
#else
        {"disableWpa3CompatibleMode", false},
#endif
#if CONFIG_ESP_WIFI_11KV_SUPPORT && CONFIG_ESP_WIFI_RRM_SUPPORT
        {"rmEnabled", true},
#else
        {"rmEnabled", false},
#endif
#if CONFIG_ESP_WIFI_11KV_SUPPORT && CONFIG_ESP_WIFI_WNM_SUPPORT
        {"btmEnabled", true},
#else
        {"btmEnabled", false},
#endif
#if CONFIG_ESP_WIFI_MBO_SUPPORT
        {"mboEnabled", true},
#else
        {"mboEnabled", false},
#endif
#if CONFIG_ESP_WIFI_11R_SUPPORT
        {"ftEnabled", true},
#else
        {"ftEnabled", false},
#endif
#if CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA
        {"oweEnabled", true},
#else
        {"oweEnabled", false},
#endif
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE && CONFIG_ESP_WIFI_ENABLE_SAE_H2E
        {"saeH2eIdentifier", true},
#else
        {"saeH2eIdentifier", false},
#endif
    };
    static const char *const pmf[] = {
#if CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
        "disabled",
#endif
        "optional", "required",
    };
    static const char *const pk[] = {
        "disabled",
#if CONFIG_ESP_WIFI_ENABLE_SAE_PK
        "automatic", "only",
#endif
    };
    static const char *const pwe[] = {
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
        "hunting-and-pecking",
#if CONFIG_ESP_WIFI_ENABLE_SAE_H2E
        "hash-to-element", "both",
#endif
#endif
        NULL,
    };
    static const char *const auth_modes[] = {"open", "wep", "wpa", "wpa2", "wpa/wpa2",
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
        "wpa3", "wpa2/wpa3",
#endif
#if CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA
        "owe",
#endif
#if CONFIG_ESP_WIFI_WAPI_PSK
        "wapi",
#endif
    };
    JSGCRef result_ref, value_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i)
        if (!esp32_mquickjs_set_property_ref(ctx, result, flags[i].name, JS_NewBool(flags[i].available))) goto fail;
    *value = wifi_capability_strings(ctx, pmf, sizeof(pmf) / sizeof(pmf[0]));
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "pmf", *value)) goto fail;
    *value = wifi_capability_strings(ctx, pwe, sizeof(pwe) / sizeof(pwe[0]) - 1);
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "saePwe", *value)) goto fail;
    *value = wifi_capability_strings(ctx, pk, sizeof(pk) / sizeof(pk[0]));
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "saePkModes", *value)) goto fail;
    *value = wifi_capability_strings(ctx, auth_modes, sizeof(auth_modes) / sizeof(auth_modes[0]));
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "minimumAuthModes", *value)) goto fail;
    JS_PopGCRef(ctx, &value_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_ap_capabilities(JSContext *ctx)
{
    static const struct { const char *name; bool available; } flags[] = {
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        {"binarySsid", true}, {"driver", true},
#else
        {"binarySsid", false}, {"driver", false},
#endif
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT && CONFIG_ESP_WIFI_ENABLE_WPA3_SAE && CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT && CONFIG_SOC_WIFI_GCMP_SUPPORT && CONFIG_ESP_WIFI_GCMP_SUPPORT && CONFIG_ESP_WIFI_ENABLE_SAE_H2E
        {"saeExt", true},
#else
        {"saeExt", false},
#endif
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT && CONFIG_ESP_WIFI_ENABLE_WPA3_SAE && CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT && CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
        {"wpa3CompatibleMode", true},
#else
        {"wpa3CompatibleMode", false},
#endif
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT && CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
        {"bssMaxIdle", true},
#else
        {"bssMaxIdle", false},
#endif
    };
    static const char *const modes[] = {
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        "open", "wpa", "wpa2", "wpa/wpa2",
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE && CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
        "wpa3", "wpa2/wpa3",
#endif
#if CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_SOFTAP
        "owe",
#endif
#endif
        NULL,
    };
    static const char *const ciphers[] = {
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        "tkip", "ccmp", "tkip/ccmp",
#if CONFIG_ESP_WIFI_GCMP_SUPPORT
        "gcmp", "gcmp-256",
#endif
#endif
        NULL,
    };
    static const char *const pmf[] = {
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        "optional", "required", "disabled",
#endif
        NULL,
    };
    static const char *const pwe[] = {
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT && CONFIG_ESP_WIFI_ENABLE_WPA3_SAE && CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
        "hunting-and-pecking",
#if CONFIG_ESP_WIFI_ENABLE_SAE_H2E
        "hash-to-element", "both",
#endif
#endif
        NULL,
    };
    JSGCRef result_ref, value_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i)
        if (!esp32_mquickjs_set_property_ref(ctx, result, flags[i].name, JS_NewBool(flags[i].available))) goto fail;
    *value = wifi_capability_strings(ctx, modes, sizeof(modes) / sizeof(modes[0]) - 1);
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "authModes", *value)) goto fail;
    *value = wifi_capability_strings(ctx, ciphers, sizeof(ciphers) / sizeof(ciphers[0]) - 1);
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "pairwiseCiphers", *value)) goto fail;
    *value = wifi_capability_strings(ctx, pmf, sizeof(pmf) / sizeof(pmf[0]) - 1);
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "pmf", *value)) goto fail;
    *value = wifi_capability_strings(ctx, pwe, sizeof(pwe) / sizeof(pwe[0]) - 1);
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "saePwe", *value)) goto fail;
    if (!esp32_mquickjs_set_property_ref(ctx, result, "beaconIntervalQuantumMs",
            JS_NewFloat64(ctx, ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU * 1.024)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "maximumBeaconIntervalMs",
            JS_NewFloat64(ctx, ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU * 1.024)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "maximumDtimPeriod", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_AP_DTIM_MAX)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ftmResponder",
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT
            JS_TRUE
#else
            JS_FALSE
#endif
        )) goto fail;
    JS_PopGCRef(ctx, &value_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_capability_band(JSContext *ctx, const wifi_country_t *country,
                                    bool available, bool ghz5)
{
    JSGCRef result_ref, channels_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *channels = JS_PushGCRef(ctx, &channels_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    *channels = JS_NULL;
    if (JS_IsException(*result)) goto fail;
    uint16_t end = (uint16_t)country->schan + country->nchan;
    bool enumerable = available && country->schan >= 1 && country->nchan > 0 && end <= 15;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (ghz5) enumerable = available && country->policy == WIFI_COUNTRY_POLICY_MANUAL &&
        country->wifi_5g_channel_mask != 0U;
#else
    if (ghz5) enumerable = false;
#endif
    if (enumerable) {
        *channels = JS_NewArray(ctx, 0);
        if (JS_IsException(*channels)) goto fail;
        uint32_t index = 0;
        for (uint16_t channel = 1; channel <= (ghz5 ? 177 : 14); ++channel) {
            bool allowed = channel >= country->schan && channel < end;
#if CONFIG_SOC_WIFI_SUPPORT_5G
            if (ghz5) allowed = (country->wifi_5g_channel_mask &
                esp32_mquickjs_wifi_radio_5ghz_channel_bit(channel)) != 0;
#endif
            if (!allowed) continue;
            *item = JS_NewUint32(ctx, channel);
            if (JS_IsException(*item) ||
                JS_IsException(JS_SetPropertyUint32(ctx, *channels, index++, *item))) goto fail;
        }
    }
    if (!esp32_mquickjs_set_property_ref(ctx, result, "band", JS_NewString(ctx, ghz5 ? "5GHz" : "2.4GHz")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channels", *channels)) goto fail;
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &channels_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &channels_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.capabilities() takes no arguments");
    static const char *const modes[] = {"station",
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        "softAP", "station+softAP",
#endif
    };
    static const char *const interfaces[] = {"station",
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        "access-point",
#endif
    };
    static const char *const namespaces[] = {
        "diagnostics",
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
        "enterprise",
#endif
#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT || CONFIG_ESP_WIFI_11R_SUPPORT
        "roaming",
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
        "smartConfig", "wps",
#if CONFIG_ESP_WIFI_DPP_SUPPORT
        "dpp",
#endif
#endif
#if CONFIG_ESP_WIFI_WAPI_PSK
        "wapi",
#endif
        "rawTx",
        "driver",
        "vendorIe",
        "action",
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
        "nan",
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
        "twt",
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && (CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT || (CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT))
        "ftm",
#endif
        "monitor",
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
        "csi",
#endif
        NULL,
    };
    /* These flags describe callable framework features, not SDK symbol presence. */
    static const struct { const char *name; bool available; } features[] = {
        {"station", true}, {"scan", true}, {"wakeLock", true}, {"setMac", true},
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        {"accessPoint", true}, {"apsta", true},
#else
        {"accessPoint", false}, {"apsta", false},
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
        {"csi", true},
#else
        {"csi", false},
#endif
        {"watch", true}, {"configure", true}, {"driverRestart", true}, {"vendorIe", true},
        {"promiscuous", true}, {"rawTx", true}, {"actionTx", true},
        {"remainOnChannel", true},
#if CONFIG_ESP_WIFI_WAPI_PSK
        {"wapi", true},
#else
        {"wapi", false},
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
        {"ftmInitiator", true},
#else
        {"ftmInitiator", false},
#endif
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT
        {"ftmResponder", true},
#else
        {"ftmResponder", false},
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
        {"individualTwt", true},
        {"broadcastTwt", true},
#else
        {"individualTwt", false},
        {"broadcastTwt", false},
#endif
#if CONFIG_ESP_WIFI_RRM_SUPPORT
        {"rrm", true},
#else
        {"rrm", false},
#endif
#if CONFIG_ESP_WIFI_WNM_SUPPORT
        {"wnm", true},
#else
        {"wnm", false},
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
        {"smartConfig", true},
#else
        {"smartConfig", false},
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
        {"nan", true},
#else
        {"nan", false},
#endif
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
        {"mesh", true},
#else
        {"mesh", false},
#endif
        {"multipleAntennas", false},
    };
    wifi_country_t country;
    esp_err_t country_error = esp32_mquickjs_wifi_radio_get_country(&country);
    JSGCRef result_ref, value_ref, bands_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    JSValue *bands = JS_PushGCRef(ctx, &bands_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "apiVersion", JS_NewString(ctx, "wifi/1")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "idfVersion", JS_NewString(ctx, esp_get_idf_version())) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "countryError",
            country_error == ESP_OK ? JS_NULL : JS_NewInt32(ctx, country_error))) goto fail;
    *value = wifi_ap_capabilities(ctx);
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "accessPointOptions", *value)) goto fail;
    *value = wifi_station_capabilities(ctx);
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "stationOptions", *value)) goto fail;
    *value = wifi_capability_strings(ctx, modes, sizeof(modes) / sizeof(modes[0]));
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "modes", *value)) goto fail;
    *value = wifi_capability_strings(ctx, interfaces, sizeof(interfaces) / sizeof(interfaces[0]));
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "interfaces", *value)) goto fail;
    *value = wifi_capability_strings(ctx, namespaces, sizeof(namespaces) / sizeof(namespaces[0]) - 1);
    if (JS_IsException(*value) || !esp32_mquickjs_set_property_ref(ctx, result, "namespaces", *value)) goto fail;
    *value = JS_NewObject(ctx);
    if (JS_IsException(*value)) goto fail;
    for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); ++i)
        if (!esp32_mquickjs_set_property_ref(ctx, value, features[i].name, JS_NewBool(features[i].available))) goto fail;
    if (!esp32_mquickjs_set_property_ref(ctx, result, "features", *value)) goto fail;
    *value = JS_NewObject(ctx);
    if (JS_IsException(*value) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "maxWatchers", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_MAX_WATCHERS)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "maxWatchCapacity", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_MAX_WATCH_CAPACITY)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "watchIngressCapacity", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_WATCH_INGRESS_CAPACITY)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "watchNeighborSlots", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_WATCH_NEIGHBOR_SLOTS)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "watchMaxNeighbors", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_WATCH_MAX_NEIGHBORS)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "watchMaxReportBytes", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_WATCH_MAX_REPORT_BYTES)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "maxWakeLocks", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_MAX_WAKE_LOCKS)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "maxScanRecords", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_MAX_SCAN_RECORDS)) ||
        !esp32_mquickjs_set_property_ref(ctx, value, "maxSoftApClients", JS_NewUint32(ctx,
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
            ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS
#else
            0
#endif
        )) || !esp32_mquickjs_set_property_ref(ctx, result, "limits", *value)) goto fail;
    *bands = JS_NewArray(ctx, 0);
    if (JS_IsException(*bands)) goto fail;
    *value = wifi_capability_band(ctx, &country, country_error == ESP_OK, false);
    if (JS_IsException(*value) || JS_IsException(JS_SetPropertyUint32(ctx, *bands, 0, *value))) goto fail;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    *value = wifi_capability_band(ctx, &country, country_error == ESP_OK, true);
    if (JS_IsException(*value) || JS_IsException(JS_SetPropertyUint32(ctx, *bands, 1, *value))) goto fail;
#endif
    if (!esp32_mquickjs_set_property_ref(ctx, result, "bands", *bands)) goto fail;
    JS_PopGCRef(ctx, &bands_ref);
    JS_PopGCRef(ctx, &value_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &bands_ref);
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

/* Immutable build descriptions; these do not acquire a lease, initialize the
 * driver, probe RF, or promise admission after this function returns. */
static const struct {
    const char *path, *symbols, *requirements;
    bool available;
} wifi_driver_operations[] = {
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    {"wifi.driver.getStatisticsConfig", "", "started stable Radio|actual native storage observation|C5", true},
    {"wifi.driver.configureRxStatistics", "esp_wifi_enable_rx_statistics", "started stable Radio|exact framework owners|idle helpers|C5", true},
    {"wifi.driver.setTxStatistics", "esp_wifi_enable_tx_statistics", "started stable Radio|exact framework owners|idle helpers|C5", true},
#else
    {"wifi.driver.getStatisticsConfig", "", "C5 only", false},
    {"wifi.driver.configureRxStatistics", "esp_wifi_enable_rx_statistics", "C5 only", false},
    {"wifi.driver.setTxStatistics", "esp_wifi_enable_tx_statistics", "C5 only", false},
#endif
    {"wifi.driver.restore", "esp_wifi_restore", "initialized and fully stopped|zero owners; no unrelated native cleanup or saved restart|healthy or exact mode/protocol/bandwidth fault; explicit restore retry resumes failed suffix", true},
    {"wifi.driver.capabilities", "", "any Radio state", true},
    {"wifi.driver.status", "", "any Radio state", true},
    {"wifi.driver.restart", "esp_wifi_stop|esp_wifi_deinit|esp_wifi_init|esp_wifi_start", "clean uninitialized or healthy stopped/off with zero owners; healthy unassociated Enterprise Station with exact managed helper owners|same Enterprise profile restored before lease publication; active/saved AP policy requires allowApRestart|cold source selects Station/RAM; off returns off|explicit complete-checkpoint retry; retained EAP clear precedes STOP; unproven init/native ownership rejects", true},
#if CONFIG_SOC_WIFI_HE_SUPPORT
    {"wifi.driver.setBssColorCollisionReporting", "esp_wifi_enable_bsscolor_collision_detection", "started Station|exact framework owners|HE target", true},
#else
    {"wifi.driver.setBssColorCollisionReporting", "esp_wifi_enable_bsscolor_collision_detection", "started Station|exact framework owners|HE target", false},
#endif
    {"wifi.driver.setDynamicCarrierSense", "esp_wifi_set_dynamic_cs", "started|exact framework owners", true},
    {"wifi.driver.configure11bRate", "esp_wifi_config_11b_rate", "initialized and fully stopped|zero owners|selected interface enabled", true},
#if CONFIG_ESP_COEX_POWER_MANAGEMENT
    {"wifi.driver.setCoexistencePowerManagement", "esp_wifi_coex_pwr_configure", "initialized and stable|exact framework owners when started; zero owners when stopped", true},
#else
    {"wifi.driver.setCoexistencePowerManagement", "esp_wifi_coex_pwr_configure", "initialized and stable|exact framework owners when started; zero owners when stopped", false},
#endif
    {"wifi.driver.getEventMask", "esp_wifi_get_event_mask", "initialized and stable", true},
    {"wifi.driver.setEventMask", "esp_wifi_set_event_mask|esp_wifi_get_event_mask", "initialized and stable|exact framework owners|only none or AP probe-request filtering", true},
    {"wifi.driver.setStorage", "esp_wifi_set_storage", "initialized and fully stopped|zero owners|healthy or exact storage-write fault", true},
    {"wifi.driver.disablePmf", "esp_wifi_disable_pmf_config|esp_wifi_get_config", "initialized and fully stopped|zero owners|eligible existing interface authentication", true},
    {"wifi.driver.setMode", "esp_wifi_set_mode|esp_wifi_get_mode", "initialized and fully stopped|zero owners", true},
    {"wifi.driver.getAntenna", "esp_phy_get_ant", "initialized and stable", true},
    {"wifi.driver.getAntennaGpio", "esp_phy_get_ant_gpio", "initialized and stable", true},
    {"wifi.driver.setAntenna", "esp_phy_set_ant|esp_phy_get_ant", "initialized and fully stopped|zero owners|BLE closed; shared PHY idle", true},
    {"wifi.driver.setAntennaGpio", "esp_phy_set_ant_gpio|esp_phy_get_ant_gpio", "initialized and fully stopped|zero owners|BLE closed; shared PHY idle|available GPIO routes", true},
    {"wifi.driver.setConnectionlessWakeInterval", "esp_wifi_connectionless_module_set_wake_interval", "initialized and stable|exact framework owners|no live ESP-NOW interval owner", true},
    {"wifi.driver.getMode", "esp_wifi_get_mode", "initialized and stable", true},
    {"wifi.driver.getCountry", "esp_wifi_get_country", "initialized and stable", true},
    {"wifi.driver.setCountryDetails", "esp_wifi_set_country|esp_wifi_get_country", "initialized and fully stopped|zero owners", true},
    {"wifi.driver.getInterfaceConfig", "esp_wifi_get_config", "initialized and stable|secret read requires the explicit build gate", true},
    {"wifi.driver.setInterfaceConfig", "esp_wifi_set_config|esp_wifi_get_config", "initialized and fully stopped|zero owners|selected interface enabled", true},
    {"wifi.driver.getChannel", "esp_wifi_get_channel", "initialized and stable|valid current-channel observation", true},
    {"wifi.driver.getHomeChannel", "esp_wifi_get_home_channel", "started and stable", true},
    {"wifi.driver.getBand", "esp_wifi_get_band", "initialized and stable", true},
    {"wifi.driver.getBandMode", "esp_wifi_get_band_mode", "initialized and stable", true},
    {"wifi.driver.setBand", "esp_wifi_set_band|esp_wifi_get_band", "started Station-only|exact framework owners; no fixed channel|AUTO band mode|unassociated for a changed value", true},
    {"wifi.driver.setBandMode", "esp_wifi_set_band_mode|esp_wifi_get_band_mode", "started Station-only|exact framework owners; no fixed channel|unassociated for a changed value", true},
    {"wifi.driver.getPowerSave", "esp_wifi_get_ps", "initialized and stable", true},
    {"wifi.driver.getTxPower", "esp_wifi_get_max_tx_power", "started and stable", true},
    {"wifi.driver.getRssi", "esp_wifi_sta_get_rssi", "started Station|associated", true},
    {"wifi.driver.getAid", "esp_wifi_sta_get_aid", "started Station|associated", true},
    {"wifi.driver.getNegotiatedPhy", "esp_wifi_sta_get_negotiated_phymode", "started Station|associated", true},
    {"wifi.driver.getTsfTime", "esp_wifi_get_tsf_time", "started and stable|selected interface enabled", true},
    {"wifi.driver.getInactiveTime", "esp_wifi_get_inactive_time", "started and stable|selected interface enabled", true},
    {"wifi.driver.getScanParameters", "esp_wifi_get_scan_parameters", "started stable Station|actual native observation", true},
    {"wifi.driver.setScanParameters", "esp_wifi_set_scan_parameters|esp_wifi_get_scan_parameters", "started stable Station|idle helpers|exact framework owners|readback rollback|restart preservation", true},
    {"wifi.driver.setInactiveTime", "esp_wifi_set_inactive_time|esp_wifi_get_inactive_time", "started and stable|exact framework owners|selected interface enabled", true},
    {"wifi.driver.setRssiThreshold", "esp_wifi_set_rssi_threshold", "initialized and stable|exact framework owners|Station interface enabled", true},
    {"wifi.driver.getProtocol", "esp_wifi_get_protocol", "initialized and stable|enabled interface; single band", true},
    {"wifi.driver.getProtocols", "esp_wifi_get_protocols", "initialized and stable|enabled interface", true},
    {"wifi.driver.getBandwidth", "esp_wifi_get_bandwidth", "initialized and stable|enabled interface; single band", true},
    {"wifi.driver.getBandwidths", "esp_wifi_get_bandwidths", "initialized and stable|enabled interface", true},
    {"wifi.driver.setProtocol", "esp_wifi_set_protocol|esp_wifi_get_protocol", "initialized and fully stopped|zero owners|enabled interface; single band", true},
    {"wifi.driver.setProtocols", "esp_wifi_set_protocols|esp_wifi_get_protocols", "initialized and fully stopped|zero owners|enabled interface", true},
    {"wifi.driver.setBandwidth", "esp_wifi_set_bandwidth|esp_wifi_get_bandwidth", "initialized and fully stopped|zero owners|enabled interface; single band", true},
    {"wifi.driver.setBandwidths", "esp_wifi_set_bandwidths|esp_wifi_get_bandwidths", "initialized and fully stopped|zero owners|enabled interface", true},
    {"wifi.driver.configureTxRate", "esp_wifi_config_80211_tx", "initialized and fully stopped|zero owners|enabled interface; target-supported PHY and rate", true},
    {"wifi.driver.txRateStatus", "", "valid interface|framework history only", true},
};

static JSValue wifi_driver_capability_list(JSContext *ctx, const char *text)
{
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    uint32_t index = 0;
    while (*text) {
        const char *end = strchr(text, '|');
        size_t length = end != NULL ? (size_t)(end - text) : strlen(text);
        *item = JS_NewStringLen(ctx, text, length);
        if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *array, index++, *item))) goto fail;
        if (end == NULL) break;
        text = end + 1;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_driver_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.driver.capabilities() takes no arguments");
    static const char *const scan_fields[] = {
        "channel", "showHidden", "mode", "activeMinMs", "activeMaxMs", "passiveMs", "timeoutMs",
        "ssid", "bssid", "homeChannelDwellMs", "coexistenceBackgroundScan", "maxRecords", "channels",
    };
    static const char *const tx_fields[] = {"phy", "rate", "ersu", "dcm"};
    JSGCRef root_ref, value_ref, array_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    *root = JS_NewObject(ctx);
    if (JS_IsException(*root)) goto fail;
#define SET(object, key, expression) do { \
    if (!esp32_mquickjs_set_property_ref(ctx, object, key, expression)) goto fail; \
} while (0)
    SET(root, "apiVersion", JS_NewString(ctx, "wifi-driver/1"));
    SET(root, "target", JS_NewString(ctx, CONFIG_IDF_TARGET));
    SET(root, "idfVersion", JS_NewString(ctx, esp_get_idf_version()));
#if CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK
    SET(root, "secretReadback", JS_TRUE);
#else
    SET(root, "secretReadback", JS_FALSE);
#endif
    SET(root, "stationOptions", wifi_station_capabilities(ctx));
    SET(root, "accessPointOptions", wifi_ap_capabilities(ctx));
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    for (size_t i = 0; i < sizeof(wifi_driver_operations) / sizeof(wifi_driver_operations[0]); ++i) {
        *value = JS_NewObject(ctx);
        if (JS_IsException(*value)) goto fail;
        SET(value, "jsPath", JS_NewString(ctx, wifi_driver_operations[i].path));
        SET(value, "idfSymbols", wifi_driver_capability_list(ctx, wifi_driver_operations[i].symbols));
        SET(value, "stateRequirements", wifi_driver_capability_list(ctx, wifi_driver_operations[i].requirements));
        SET(value, "available", JS_NewBool(wifi_driver_operations[i].available));
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array, i, *value))) goto fail;
    }
    SET(root, "operations", *array);
    *value = JS_NewObject(ctx);
    if (JS_IsException(*value)) goto fail;
    SET(value, "stationFields", wifi_capability_strings(ctx, wifi_station_driver_keys,
        sizeof(wifi_station_driver_keys) / sizeof(wifi_station_driver_keys[0])));
    SET(value, "accessPointFields", wifi_capability_strings(ctx, wifi_ap_driver_keys,
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        sizeof(wifi_ap_driver_keys) / sizeof(wifi_ap_driver_keys[0])
#else
        0
#endif
    ));
    SET(value, "scanFields", wifi_capability_strings(ctx, scan_fields, sizeof(scan_fields) / sizeof(scan_fields[0])));
    SET(value, "txRateFields", wifi_capability_strings(ctx, tx_fields, sizeof(tx_fields) / sizeof(tx_fields[0])));
    SET(root, "configSchemas", *value);
#undef SET
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &value_ref);
    return JS_PopGCRef(ctx, &root_ref);
fail:
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &root_ref);
    return JS_EXCEPTION;
}
#endif
