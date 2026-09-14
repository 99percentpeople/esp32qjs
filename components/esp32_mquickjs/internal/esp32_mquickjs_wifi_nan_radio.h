#pragma once
#include "esp32_mquickjs_wifi_nan_ndp.h"
#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"
#include "esp_wifi_types.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wifi_nan_sdk.h"
#include "esp32_mquickjs_wifi_nan_query.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
typedef struct {
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
    size_t reserved_bytes;
    bool start_attempted, start_accepted, ready, closing;
    bool stopped, native_reset, netif_retired, observer_retired;
    bool mode_restored, storage_restored;
    bool usd;
} esp32_mquickjs_wifi_nan_radio_status_t;

/* Background task only; no JS callbacks. Requires a stopped, healthy Radio
 * with zero owners. All native writes and final release use the Radio mutex.
 * A nonzero output token on failure transfers cleanup duty to the caller;
 * no second start may overwrite it. The native observer's opaque storage
 * must live until close returns success and clears the operation token. */
esp_err_t esp32_mquickjs_wifi_radio_nan_begin(const wifi_nan_sync_config_t *config,
    esp32_mquickjs_wifi_nan_sdk_observer_fn observer, void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_nan_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_nan_usd_begin(
    esp32_mquickjs_wifi_nan_sdk_observer_fn observer, void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_nan_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_nan_poll(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_nan_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status);
#if CONFIG_ESP_WIFI_NAN_PAIRING
esp_err_t esp32_mquickjs_wifi_radio_nan_bootstrap_send(const esp32_mquickjs_wifi_radio_operation_t *token,
    wifi_nan_followup_params_t *params, uint32_t identity, uint32_t *context, bool response, bool accept);
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t service_id, esp32_mquickjs_wifi_nan_credentials_t *out);
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_commit(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity, uint32_t *credential_id);
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_start(const esp32_mquickjs_wifi_radio_operation_t *token,
    const esp32_mquickjs_wifi_nan_pairing_config_t *config, esp32_mquickjs_wifi_nan_pasn_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_poll(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity, bool close, esp32_mquickjs_wifi_nan_pasn_status_t *status);
#endif
/* Session's single worker serializes service creation/cancellation and owns
 * raw IDs. No JS-supplied ID reaches these mutation boundaries. */
esp_err_t esp32_mquickjs_wifi_radio_nan_service_start(const esp32_mquickjs_wifi_radio_operation_t *token,
    const wifi_nan_publish_cfg_t *publish, const wifi_nan_subscribe_cfg_t *subscribe, uint8_t *id);
esp_err_t esp32_mquickjs_wifi_radio_nan_service_close(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t id, bool *cancelled);
esp_err_t esp32_mquickjs_wifi_radio_nan_message_send(const esp32_mquickjs_wifi_radio_operation_t *token,
    wifi_nan_followup_params_t *params, uint32_t identity, uint32_t *context);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
esp_err_t esp32_mquickjs_wifi_radio_nan_query(const esp32_mquickjs_wifi_radio_operation_t *token,
    const esp32_mquickjs_wifi_nan_query_t *query, esp32_mquickjs_wifi_nan_query_result_t *out);
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_request(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t service_id, wifi_nan_datapath_req_t *request, uint32_t *identity);
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_response(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity, bool accept, const uint8_t *ssi, uint16_t length);
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_end(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity);
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_release(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity, esp32_mquickjs_wifi_nan_ndp_status_t *status);
#endif
#endif
