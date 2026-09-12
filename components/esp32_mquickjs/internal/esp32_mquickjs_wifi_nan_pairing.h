#pragma once
#include "esp32_mquickjs_wifi_nan_discovery.h"
#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_PAIRING
#define ESP32_MQUICKJS_NAN_PAIRING_HANDLES 8U
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_PEERS 8U
#define ESP32_MQUICKJS_NAN_PAIRING_CONFIRM_MS 30000U
typedef struct esp32_mquickjs_wifi_nan_pairing esp32_mquickjs_wifi_nan_pairing_t;
typedef struct {
    uint32_t identity, service_identity, session_identity;
    uint32_t credential_id;
    uint8_t service_id, peer_service_id, peer[6];
    size_t reserved_bytes;
    esp_err_t error, cleanup_error;
    bool initiator, activated, confirmed, rejected, start_attempted;
    bool ready, closing, retired, timed_out, verification;
    bool bootstrap, incoming, delivered, bootstrap_attempted, bootstrap_sent, bootstrap_tx_pending;
    bool peer_responded, peer_accepted;
    esp32_mquickjs_wifi_nan_pasn_status_t native;
} esp32_mquickjs_wifi_nan_pairing_status_t;
/* Preparation has no authentication side effect. The confirmed PIN is held
 * only until the single Session worker submits it to the checked SDK command. */
esp_err_t esp32_mquickjs_wifi_nan_pairing_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    uint8_t peer_service_id, const uint8_t peer[6], bool initiator, uint32_t timeout_ms,
    esp32_mquickjs_wifi_nan_pairing_t **out);
esp_err_t esp32_mquickjs_wifi_nan_pairing_activate(esp32_mquickjs_wifi_nan_pairing_t *pairing);
/* Selection is revalidated on the Wi-Fi task after explicit consent. */
esp_err_t esp32_mquickjs_wifi_nan_pairing_select(esp32_mquickjs_wifi_nan_pairing_t *pairing, uint32_t credential_id);
esp_err_t esp32_mquickjs_wifi_nan_pairing_request(esp32_mquickjs_wifi_nan_pairing_t *pairing);
esp_err_t esp32_mquickjs_wifi_nan_pairing_receive(esp32_mquickjs_wifi_nan_discovery_t *service,
    esp32_mquickjs_wifi_nan_pairing_t **out);
bool esp32_mquickjs_wifi_nan_pairing_deliver(esp32_mquickjs_wifi_nan_pairing_t *pairing);
void esp32_mquickjs_wifi_nan_pairing_unreserve(esp32_mquickjs_wifi_nan_pairing_t *pairing);
bool esp32_mquickjs_wifi_nan_pairing_retain(esp32_mquickjs_wifi_nan_pairing_t *pairing);
void esp32_mquickjs_wifi_nan_pairing_release(void *pairing);
esp_err_t esp32_mquickjs_wifi_nan_pairing_confirm(esp32_mquickjs_wifi_nan_pairing_t *pairing,
    bool accept, uint32_t pin);
void esp32_mquickjs_wifi_nan_pairing_close(esp32_mquickjs_wifi_nan_pairing_t *pairing, bool timeout);
void esp32_mquickjs_wifi_nan_pairing_status(esp32_mquickjs_wifi_nan_pairing_t *pairing,
    esp32_mquickjs_wifi_nan_pairing_status_t *status);
#endif
