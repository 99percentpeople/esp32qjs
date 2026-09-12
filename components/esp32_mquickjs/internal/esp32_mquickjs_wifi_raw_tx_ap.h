#pragma once
#include "esp32_mquickjs_wifi_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_tx_rate.h"

/* Internal AP/APSTA pre-start handoff. No implicit initialization, STOP, mode or
 * configuration change: require a healthy configured/stopped AP or APSTA and zero owners.
 * Begin reserves the ordinary lifecycle. The runtime then copies/validates saved
 * AP config and prepares its helper under that token before calling start. */
esp_err_t esp32_mquickjs_wifi_radio_begin_raw_tx_ap_rate(
    const wifi_tx_rate_config_t *rate, esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
/* expected is the secure caller-owned copy from copy_stopped_ap_configuration.
 * Rechecks it immediately before writing rate/starting. On success the lifecycle
 * transfers to the exclusive temporary-rate owner. On error, retain both outputs:
 * an acquired temporary-rate lease can also consume token despite failed START;
 * quiesce handles either retained-lifecycle or retained-rate ownership. */
esp_err_t esp32_mquickjs_wifi_radio_start_raw_tx_ap_rate(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode, const wifi_config_t *expected,
    const wifi_tx_rate_config_t *rate, esp32_mquickjs_wifi_radio_lease_t *lease,
    uint8_t *actual_channel);
/* Caller has retired the original SDK packet and excludes another Session worker.
 * Complete STOP/events and rate restore, then atomically exchange the real lease
 * for a lifecycle reservation. Keep that token across AP helper retirement; only
 * afterwards finish_lifecycle(token,false). Failure preserves all outstanding
 * ownership. Also consumes partial start outputs; never releases foreign owners. */
esp_err_t esp32_mquickjs_wifi_radio_quiesce_raw_tx_ap_rate(
    esp32_mquickjs_wifi_radio_lease_t *lease, esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Runtime-task helper ownership retained by native Session storage, never JS. */
typedef struct {
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    bool owned;
    uint8_t mode;
    bool station_cleanup_pending;
} esp32_mquickjs_wifi_raw_tx_ap_context_t;
bool esp32_mquickjs_wifi_raw_tx_ap_ready(void);
esp_err_t esp32_mquickjs_wifi_ap_open_raw_tx_rate(
    esp32_mquickjs_wifi_raw_tx_ap_context_t *context, const wifi_tx_rate_config_t *rate,
    uint8_t channel, esp32_mquickjs_wifi_radio_lease_t *lease, uint8_t *actual_channel,
    const char **stage);
esp_err_t esp32_mquickjs_wifi_ap_close_raw_tx_rate(
    esp32_mquickjs_wifi_raw_tx_ap_context_t *context, esp32_mquickjs_wifi_radio_lease_t *lease,
    bool physically_terminated, const char **stage);
#endif
