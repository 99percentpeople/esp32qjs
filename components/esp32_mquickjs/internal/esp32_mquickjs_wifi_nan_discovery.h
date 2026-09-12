#pragma once
#include "esp32_mquickjs_wifi_nan_session.h"
#include <string.h>
#include "esp32_mquickjs_wifi_nan_service_config.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#define ESP32_MQUICKJS_NAN_SERVICE_HANDLES 8U
typedef struct esp32_mquickjs_wifi_nan_discovery esp32_mquickjs_wifi_nan_discovery_t;
typedef union {
    wifi_nan_publish_cfg_t publish;
    wifi_nan_subscribe_cfg_t subscribe;
} esp32_mquickjs_wifi_nan_service_config_t;
typedef struct {
    uint32_t identity, session_identity, dropped;
    uint32_t send_identity;
    uint32_t pairing_requests_dropped;
    uint8_t native_id;
    esp_err_t error, cleanup_error;
    size_t reserved_bytes;
    bool publish, activated, creating, ready, closing, cancelled, retired, timed_out;
    bool send_pending, send_cleanup_pending;
    bool security_required, group_data_protection, group_management_protection;
    bool pairing_enabled, pairing_request_pending;
    uint8_t credential_count;
    uint16_t vendor_bytes;
} esp32_mquickjs_wifi_nan_discovery_status_t;
typedef struct {
    uint32_t identity, sequence;
    uint8_t native_id, peer_id, peer[6], kind, ssi_version;
    bool datapath_required, security_required, further_discovery, gas, ndpe;
    uint16_t ssi_len;
    uint8_t ssi[ESP_WIFI_MAX_FUP_SSI_LEN];
} esp32_mquickjs_wifi_nan_discovery_event_t;
/* Deep-copies SSI, credentials and vendor storage. USD must match the parent
 * mode and rejects unsupported security/NDP/filter/vendor options. */
esp_err_t esp32_mquickjs_wifi_nan_discovery_create(esp32_mquickjs_wifi_nan_session_t *parent,
    bool publish, const esp32_mquickjs_wifi_nan_service_config_t *config, uint32_t timeout_ms,
    esp32_mquickjs_wifi_nan_discovery_t **out);
bool esp32_mquickjs_wifi_nan_discovery_retain(esp32_mquickjs_wifi_nan_discovery_t *service);
void esp32_mquickjs_wifi_nan_discovery_release(void *service);
esp_err_t esp32_mquickjs_wifi_nan_discovery_activate(esp32_mquickjs_wifi_nan_discovery_t *service,
    esp32_mquickjs_event_queue_t *queue);
void esp32_mquickjs_wifi_nan_discovery_close(esp32_mquickjs_wifi_nan_discovery_t *service);
void esp32_mquickjs_wifi_nan_discovery_queue_closed(void *service);
void esp32_mquickjs_wifi_nan_discovery_status(esp32_mquickjs_wifi_nan_discovery_t *service,
    esp32_mquickjs_wifi_nan_discovery_status_t *status);
#if CONFIG_ESP_WIFI_NAN_PAIRING
esp_err_t esp32_mquickjs_wifi_nan_discovery_credentials_request(esp32_mquickjs_wifi_nan_discovery_t *service,
    uint32_t *revision);
esp_err_t esp32_mquickjs_wifi_nan_discovery_credentials_read(esp32_mquickjs_wifi_nan_discovery_t *service,
    uint32_t revision, bool *ready, esp32_mquickjs_wifi_nan_credentials_t *out);
#endif
#endif
