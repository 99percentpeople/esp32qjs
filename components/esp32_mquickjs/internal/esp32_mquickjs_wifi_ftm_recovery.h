#pragma once
#include "esp32_mquickjs_wifi_ftm_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
#include "esp32_mquickjs_wifi_radio.h"
/* Internal recovery phases for the existing Radio/helper coordinator. No public
 * JS entry yet. Healthy/readable started Station/APSTA, exact submitted FTM and
 * all managed leases required; unrelated owners reject admission. Capture must
 * precede STOP, after managed leases are released. Original Session owns FTM
 * retirement throughout. Recovery suspends normal end/collect/retire until
 * physical termination; a stopped SDK may already have freed its report.
 *
 * Fixed C3/S3/C5: matched Station STOP -> fresh TASK timer/native queue barriers -> callback
 * unregister/drain -> successful deinit. Only then mark physical termination
 * and discarded report. Shutdown returns TIMEOUT until the original worker
 * retires its lease; generation cannot advance first. No synthetic RF report,
 * force release, default configuration, implicit reconnect or runtime reset. */
esp_err_t esp32_mquickjs_wifi_radio_begin_ftm_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_ftm_token_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
bool esp32_mquickjs_wifi_radio_ftm_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_checkpoint_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
esp_err_t esp32_mquickjs_wifi_radio_stop_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_check_stopped_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_shutdown_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Remove only the recovery exception after owner drain; keep the lifecycle and
 * frozen config for ordinary rebuild/replay, or explicit central cleanup. */
esp_err_t esp32_mquickjs_wifi_radio_finish_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
#endif
