#pragma once
#include "esp32_mquickjs_wifi_nan_session.h"
#include <string.h>
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
/* Validate before reserving native owners or mutating the driver. The fixed
 * caller-supplied credentials use NCS-SK-128. Pairing capture separately adds
 * the native PASN cipher descriptor without fabricating a password or PMK. */
static inline esp_err_t esp32_mquickjs_wifi_nan_security_validate(
    const wifi_nan_discovery_security_params_t *security)
{
    if (!security) return ESP_OK;
#if !CONFIG_ESP_WIFI_NAN_SECURITY
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (security->reserved || !security->num_credentials ||
        security->num_credentials > ESP_WIFI_NAN_MAX_CREDS_PER_SVC)
        return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < security->num_credentials; ++i) {
        const wifi_nan_credential_t *credential = &security->creds[i];
        if (credential->csid != WIFI_NAN_CSID_NCS_SK_128) return ESP_ERR_NOT_SUPPORTED;
        if (credential->reserved) return ESP_ERR_INVALID_ARG;
        if (!credential->use_pmk) {
            const char *end = memchr(credential->passphrase, 0, sizeof(credential->passphrase));
            if (!end || end - credential->passphrase < 8) return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
#endif
}

static inline bool esp32_mquickjs_wifi_nan_vendor_valid(const nan_vendor_ie_t *vendor)
{
    return !vendor || (vendor->body_len <= NAN_VENDOR_IE_MAX_BODY_LEN &&
        (!vendor->body_len || vendor->body));
}
#endif
