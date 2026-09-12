#pragma once
#include "esp32_mquickjs_wifi_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Internal physical phases. Admission retains the exact original native token;
 * callers retire managed Wi-Fi helpers, never the original Raw TX owner.
 * Checkpoint freezes configuration/policies and any temporary rate predecessor. */
esp_err_t esp32_mquickjs_wifi_radio_begin_raw_tx_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_raw_tx_token_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
esp_err_t esp32_mquickjs_wifi_radio_checkpoint_raw_tx_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
bool esp32_mquickjs_wifi_radio_raw_tx_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_stop_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_check_stopped_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_shutdown_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_finish_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
#endif
