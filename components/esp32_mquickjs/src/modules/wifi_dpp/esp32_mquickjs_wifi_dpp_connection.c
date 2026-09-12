#include "esp32_mquickjs_wifi_dpp_connection.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
#include <string.h>

static bool dpp_hex_password(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (!((bytes[i] >= '0' && bytes[i] <= '9') || (bytes[i] >= 'a' && bytes[i] <= 'f') ||
            (bytes[i] >= 'A' && bytes[i] <= 'F'))) return false;
    return true;
}

esp_err_t esp32_mquickjs_wifi_dpp_connection_prepare(const esp_dpp_config_data_t *row,
    esp32_mquickjs_wifi_dpp_auth_t requested, esp32_mquickjs_wifi_dpp_auth_t *selected,
    wifi_config_t *station)
{
    if (!selected || !station) return ESP_ERR_INVALID_ARG;
    *selected = ESP32_MQUICKJS_DPP_AUTH_DEFAULT;
    memset(station, 0, sizeof(*station));
    if (!row || requested < ESP32_MQUICKJS_DPP_AUTH_DEFAULT || requested > ESP32_MQUICKJS_DPP_AUTH_SAE ||
        !row->ssid_len || row->ssid_len > sizeof(row->ssid) || row->password_len > sizeof(row->password) ||
        row->connector_len >= sizeof(row->connector) || row->net_access_key_len > sizeof(row->net_access_key) ||
        row->c_sign_key_len > sizeof(row->c_sign_key) ||
        row->akm < ESP_DPP_AKM_DPP || row->akm > ESP_DPP_AKM_PSK_SAE_DPP)
        return ESP_ERR_INVALID_ARG;
    /* The SDK Station struct has no explicit SSID length. Preserve all bytes
     * or reject; do not silently connect to the prefix of a binary SSID. */
    if (memchr(row->ssid, 0, row->ssid_len)) return ESP_ERR_NOT_SUPPORTED;
    bool connector = row->akm == ESP_DPP_AKM_DPP || row->akm == ESP_DPP_AKM_SAE_DPP ||
        row->akm == ESP_DPP_AKM_PSK_SAE_DPP;
    bool psk = row->akm == ESP_DPP_AKM_PSK || row->akm == ESP_DPP_AKM_PSK_SAE ||
        row->akm == ESP_DPP_AKM_PSK_SAE_DPP;
    bool sae = row->akm == ESP_DPP_AKM_SAE || row->akm == ESP_DPP_AKM_PSK_SAE ||
        row->akm == ESP_DPP_AKM_SAE_DPP || row->akm == ESP_DPP_AKM_PSK_SAE_DPP;
    esp32_mquickjs_wifi_dpp_auth_t auth = requested;
    if (auth == ESP32_MQUICKJS_DPP_AUTH_DEFAULT)
        auth = connector ? ESP32_MQUICKJS_DPP_AUTH_CONNECTOR : sae ? ESP32_MQUICKJS_DPP_AUTH_SAE : ESP32_MQUICKJS_DPP_AUTH_PSK;
    if ((auth == ESP32_MQUICKJS_DPP_AUTH_CONNECTOR && !connector) ||
        (auth == ESP32_MQUICKJS_DPP_AUTH_SAE && !sae) || (auth == ESP32_MQUICKJS_DPP_AUTH_PSK && !psk))
        return ESP_ERR_INVALID_ARG;
    if (auth == ESP32_MQUICKJS_DPP_AUTH_CONNECTOR) {
        if (!row->connector_len || row->connector[row->connector_len] || memchr(row->connector, 0, row->connector_len) ||
            !row->net_access_key_len || !row->c_sign_key_len) return ESP_ERR_INVALID_ARG;
    } else {
        if (!row->password_len || memchr(row->password, 0, row->password_len)) return ESP_ERR_INVALID_ARG;
        if (auth == ESP32_MQUICKJS_DPP_AUTH_PSK &&
            (row->password_len < 8 || (row->password_len == 64 && !dpp_hex_password(row->password, 64))))
            return ESP_ERR_INVALID_ARG;
        if (auth == ESP32_MQUICKJS_DPP_AUTH_SAE) {
            if (row->password_len > 63) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
            return ESP_ERR_NOT_SUPPORTED;
#endif
        }
    }
    /* No fallible operation follows the first credential copy. The exchange
     * channel is not an AP channel constraint: let Station scan find the AP. */
    memcpy(station->sta.ssid, row->ssid, row->ssid_len);
    if (auth != ESP32_MQUICKJS_DPP_AUTH_CONNECTOR)
        memcpy(station->sta.password, row->password, row->password_len);
    station->sta.threshold.authmode = auth == ESP32_MQUICKJS_DPP_AUTH_CONNECTOR ? WIFI_AUTH_DPP :
        auth == ESP32_MQUICKJS_DPP_AUTH_SAE ? WIFI_AUTH_WPA3_PSK : WIFI_AUTH_WPA2_PSK;
    station->sta.pmf_cfg.capable = true;
    station->sta.pmf_cfg.required = auth != ESP32_MQUICKJS_DPP_AUTH_PSK;
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    if (auth == ESP32_MQUICKJS_DPP_AUTH_SAE) station->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#endif
    *selected = auth;
    return ESP_OK;
}
#endif
