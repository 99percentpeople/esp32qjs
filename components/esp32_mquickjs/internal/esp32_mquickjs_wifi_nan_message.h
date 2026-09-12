#pragma once
#include "esp32_mquickjs_wifi_nan_discovery.h"
#include "esp32_mquickjs_wifi_nan_tx.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#define ESP32_MQUICKJS_NAN_MESSAGE_HANDLES 8U
typedef struct esp32_mquickjs_wifi_nan_message esp32_mquickjs_wifi_nan_message_t;
typedef struct {
    uint32_t identity, service_identity;
    uint16_t bytes;
    uint8_t service_id, peer_id, peer[6];
    esp_err_t error;
    bool activated, submit_started, submitted, done, cancelled, timed_out, retired;
    esp32_mquickjs_wifi_nan_message_tx_status_t tx;
} esp32_mquickjs_wifi_nan_message_status_t;
esp_err_t esp32_mquickjs_wifi_nan_message_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    const wifi_nan_followup_params_t *params, uint32_t timeout_ms, esp32_mquickjs_wifi_nan_message_t **out);
esp_err_t esp32_mquickjs_wifi_nan_message_activate(esp32_mquickjs_wifi_nan_message_t *message);
#if CONFIG_ESP_WIFI_NAN_PAIRING
esp_err_t esp32_mquickjs_wifi_nan_message_bootstrap_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    uint8_t peer_service_id, const uint8_t peer[6], bool response, bool accept, uint32_t timeout_ms,
    esp32_mquickjs_wifi_nan_message_t **out);
#endif
void esp32_mquickjs_wifi_nan_message_cancel(esp32_mquickjs_wifi_nan_message_t *message, bool timeout);
void esp32_mquickjs_wifi_nan_message_release(esp32_mquickjs_wifi_nan_message_t *message);
void esp32_mquickjs_wifi_nan_message_status(esp32_mquickjs_wifi_nan_message_t *message,
    esp32_mquickjs_wifi_nan_message_status_t *status);
#endif
