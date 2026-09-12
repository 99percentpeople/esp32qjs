#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

/* Private native bridge; no JS values or public API contract. reserve() runs
 * on the Wi-Fi task before SDK initialization. Copy/commit and metadata reads
 * take the persistent SDK mutex. Native detach is not a driver/event fence. */
#define ESP32QJS_DPP_URI_MAX 1024U
#define ESP32QJS_DPP_INFO_MAX 128U
#define ESP32QJS_DPP_KEY_DER_MAX 256U
#define ESP32QJS_DPP_CHANNEL_LIST_MAX 20U
#define ESP32QJS_DPP_TX_MAX 1476U
typedef struct {
    uint64_t identity;
    esp_err_t error, cleanup_error, tx_cancel_error, roc_cancel_error;
    esp_err_t tx_submit_error, roc_submit_error;
    int32_t event_id;
    uint32_t reserved_bytes, observation_drops, duplicate_results;
    uint32_t async_pending, async_active;
    uint64_t roc_operation;
    esp_err_t roc_error;
    uint32_t roc_duplicates, roc_ignored;
    uint64_t tx_operation;
    esp_err_t tx_error;
    uint32_t tx_duplicates, tx_ignored;
    esp_err_t tx_buffer_error, tx_post_error;
    uint32_t tx_wake_retries;
    bool tx_buffer_present, tx_recycling, tx_recycled;
    bool tx_queued;
    uint32_t tx_queued_bytes;
    esp_err_t chm_error, chm_cleanup_error;
    uint32_t chm_post_failures, chm_ignored_messages;
    uint8_t chm_timers_held, chm_active_arms;
    uint16_t uri_length;
    uint8_t config_count;
    bool retained, attached, closing, sdk_retired, terminal;
    bool driver_retired, event_fenced, listening;
    bool uri_available, configs_available;
    bool connection_mode, configuration_installed;
    bool roc_held, roc_completed;
    bool tx_held, tx_completed, tx_duration_completed;
} esp32_mquickjs_wifi_dpp_result_status_t;

bool esp32qjs_dpp_result_held(void);
esp_err_t esp32qjs_dpp_result_reserve(uint64_t *identity);
esp_err_t esp32qjs_dpp_result_discard_unbound(uint64_t identity);
esp_err_t esp32qjs_dpp_result_status(uint64_t identity, esp32_mquickjs_wifi_dpp_result_status_t *out);
esp_err_t esp32qjs_dpp_result_uri_copy(uint64_t identity, char *out, size_t capacity);
esp_err_t esp32qjs_dpp_result_uri_commit(uint64_t identity);
esp_err_t esp32qjs_dpp_result_config_copy(uint64_t identity, unsigned index, esp_dpp_config_data_t *out);
esp_err_t esp32qjs_dpp_result_configs_commit(uint64_t identity);
esp_err_t esp32qjs_dpp_result_release(uint64_t identity);
/* Native commands execute on the Wi-Fi task. Failed begin may return a nonzero
 * identity with cleanup obligations; the caller must retain and close it. */
esp_err_t esp32qjs_dpp_native_begin(uint64_t *identity);
/* Pure syntax/length/channel validation; does not initialize or mutate Wi-Fi. */
esp_err_t esp32qjs_dpp_bootstrap_validate(const char *channels, const char *private_key_hex_der,
    const char *info);
esp_err_t esp32qjs_dpp_native_bootstrap(uint64_t identity, const char *channels,
    const char *private_key_hex_der, const char *info);
esp_err_t esp32qjs_dpp_native_listen(uint64_t identity);
/* Fresh native identity, after capture retirement. NULL selects password
 * authentication; a row installs Connector material for Network Introduction. */
esp_err_t esp32qjs_dpp_native_select(uint64_t identity, const esp_dpp_config_data_t *row);
esp_err_t esp32qjs_dpp_native_check_connection(uint64_t identity, uint8_t expected_akm, const uint8_t bssid[6]);
esp_err_t esp32qjs_dpp_native_close(uint64_t identity);
/* Used only by the reviewed native ioctl cancellation guard. */
bool esp32qjs_dpp_is_action_callback(uintptr_t callback);
/* Called only by the pinned native off-channel event publisher, before any
 * observer queue. Bounded copy, no allocation/API mutex/protocol execution. */
void esp32qjs_dpp_tx_status_capture(const wifi_event_action_tx_status_t *event);
