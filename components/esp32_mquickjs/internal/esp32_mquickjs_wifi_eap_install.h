#pragma once
#include "esp32_mquickjs_wifi_enterprise_profile.h"
#include "esp32_mquickjs_wifi_eap_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
typedef struct {
    uint64_t identity; /* Zero if this result owns no SDK binding. Never reused. */
    bool entered, retained, enabled;
    const char *stage; /* Static non-secret identifier. */
    esp_err_t error, cleanup_error;
    esp32_mquickjs_wifi_eap_sdk_snapshot_t sdk;
} esp32_mquickjs_wifi_eap_install_result_t;

/* Native-only SDK transaction. Caller holds a live profile reference throughout
 * this synchronous call AND exclusive Radio admission; neither is JS storage.
 * Refuses foreign SDK resources or an existing binding. On partial failure the
 * result may retain an identity: keep Radio admission until clear succeeds.
 * It never replaces another binding or reclaims storage on dispatch failure. */
esp_err_t esp32_mquickjs_wifi_eap_install(esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_install_result_t *output);
/* Exact identity required. Duplicate/stale clear cannot affect a new binding.
 * A successful clear retires the SDK borrow, leaving caller's profile intact. */
esp_err_t esp32_mquickjs_wifi_eap_install_clear(uint64_t identity,
    esp32_mquickjs_wifi_eap_install_result_t *output);
esp_err_t esp32_mquickjs_wifi_eap_install_status(uint64_t identity,
    esp32_mquickjs_wifi_eap_install_result_t *output);
/* Observe on the owning native task; require this exact profile and spare
 * identity capacity before retiring an enabled source for reconstruction. */
esp_err_t esp32_mquickjs_wifi_eap_install_restart_source(uint64_t identity,
    esp32_mquickjs_wifi_eap_profile_t *profile, esp32_mquickjs_wifi_eap_install_result_t *output);
#endif
