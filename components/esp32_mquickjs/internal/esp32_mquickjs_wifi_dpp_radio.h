#pragma once
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wifi_dpp_worker.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
typedef struct {
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp32_mquickjs_wifi_dpp_worker_status_t worker;
    esp_err_t error;
    const char *stage;
    size_t reserved_bytes;
    bool channel_restored;
    bool config_restored, storage_restored, connection_borrowed;
    bool restore_requires_restart, restore_stopped, restore_started;
    bool restore_start_failed, restore_recovery_pending;
    uint32_t restore_recovery_attempts;
    esp_err_t restore_start_error;
} esp32_mquickjs_wifi_dpp_radio_status_t;

/* Background worker only. Reserves all three exact helper owners before the
 * first native command. Failure with a nonzero token transfers cleanup duty.
 * Capture never installs a credential: explicit selection/connection follows
 * native retirement. AP channel interruption requires explicit consent. */
esp_err_t esp32_mquickjs_wifi_radio_dpp_begin(
    const esp32_mquickjs_wifi_radio_lease_t owners[3], bool allow_ap_channel_change,
    const esp32_mquickjs_wifi_dpp_worker_options_t *options,
    esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_dpp_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_dpp_listen(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_dpp_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_dpp_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_dpp_prepare_close(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status);
/* Background-only explicit authorization for one retry after the known
 * dpp-restore-start SDK failure. Ordinary close never grants this permission. */
esp_err_t esp32_mquickjs_wifi_radio_dpp_recover(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_dpp_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_dpp_uri(const esp32_mquickjs_wifi_radio_operation_t *token,
    char *uri, size_t capacity, bool commit);
esp_err_t esp32_mquickjs_wifi_radio_dpp_config(const esp32_mquickjs_wifi_radio_operation_t *token,
    unsigned index, esp_dpp_config_data_t *config);
esp_err_t esp32_mquickjs_wifi_radio_dpp_configs_commit(const esp32_mquickjs_wifi_radio_operation_t *token);
esp_err_t esp32_mquickjs_wifi_radio_dpp_select(const esp32_mquickjs_wifi_radio_operation_t *token,
    const esp_dpp_config_data_t *row, esp32_mquickjs_wifi_dpp_auth_t requested, bool allow_ap_restart,
    esp32_mquickjs_wifi_dpp_auth_t *selected, wifi_config_t *station,
    esp32_mquickjs_wifi_dpp_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_dpp_connection_begin(const esp32_mquickjs_wifi_radio_operation_t *token);
esp_err_t esp32_mquickjs_wifi_radio_dpp_connection_end(const esp32_mquickjs_wifi_radio_operation_t *token);
esp_err_t esp32_mquickjs_wifi_radio_dpp_check_connection(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_auth_t selected, const uint8_t bssid[6]);
#endif
