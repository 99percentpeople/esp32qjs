#pragma once
#include "esp32_mquickjs_wifi_enterprise_profile.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
typedef enum {
    ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE = 1,
    ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE,
    ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR,
} esp32_mquickjs_wifi_eap_config_action_t;
typedef struct {
    uint64_t identity, revision;
    esp32_mquickjs_wifi_eap_config_action_t action;
} esp32_mquickjs_wifi_eap_config_token_t;
typedef struct {
    uint64_t revision;
    bool configured, busy, closing, revision_exhausted, identity_exhausted;
} esp32_mquickjs_wifi_eap_config_status_t;

/* Runtime owns the configuration; no JS/context pointer is stored. Reopening
 * requires completed retirement and consumes a fresh revision. */
esp_err_t esp32_mquickjs_wifi_eap_config_open(void);
void esp32_mquickjs_wifi_eap_config_status(esp32_mquickjs_wifi_eap_config_status_t *output);
/* Snapshot revision BEFORE running any user getter. Commit only after capture
 * and result preallocation; stale/reentrant commits leave the newer owner alone.
 * Candidate stays caller-owned; success retains a separate configured ref. */
esp_err_t esp32_mquickjs_wifi_eap_config_replace(uint64_t expected_revision,
    esp32_mquickjs_wifi_eap_profile_t *candidate);
/* All public SDK controls must begin here before touching Radio. The returned
 * profile is borrowed from this exact operation until finish (not a new ref).
 * A timeout/Future terminal alone does not authorize finish: native work must
 * have returned and no longer reference operation storage. */
esp_err_t esp32_mquickjs_wifi_eap_config_begin(uint64_t expected_revision,
    esp32_mquickjs_wifi_eap_config_action_t action, esp32_mquickjs_wifi_eap_config_token_t *token,
    esp32_mquickjs_wifi_eap_profile_t **profile);
esp_err_t esp32_mquickjs_wifi_eap_config_finish(esp32_mquickjs_wifi_eap_config_token_t *token,
    bool discard_configuration);
/* begin_close invalidates pending captures and blocks new controls while the
 * original exact operation may still finish. finish_close never drops a busy
 * operation or a configuration while Radio retains its SDK borrow. */
void esp32_mquickjs_wifi_eap_config_begin_close(void);
bool esp32_mquickjs_wifi_eap_config_finish_close(void);
#endif
