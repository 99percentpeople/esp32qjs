#pragma once
#include "esp32_mquickjs_wifi_nan_discovery.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#define ESP32_MQUICKJS_NAN_PATH_HANDLES 8U
typedef struct esp32_mquickjs_wifi_nan_path esp32_mquickjs_wifi_nan_path_t;
typedef struct {
    uint32_t identity, service_identity, session_identity;
    esp32_mquickjs_wifi_nan_ndp_status_t native;
    esp_err_t error, cleanup_error;
    size_t reserved_bytes;
    uint16_t ssi_len;
    uint8_t ssi[ESP_WIFI_MAX_SVC_SSI_LEN];
    bool outgoing, activated, delivered, ready, closing, retired, timed_out;
    bool response_requested, accept;
} esp32_mquickjs_wifi_nan_path_status_t;
esp_err_t esp32_mquickjs_wifi_nan_path_create(esp32_mquickjs_wifi_nan_discovery_t *service,
    const wifi_nan_datapath_req_t *request, uint32_t timeout_ms, esp32_mquickjs_wifi_nan_path_t **out);
esp_err_t esp32_mquickjs_wifi_nan_path_activate(esp32_mquickjs_wifi_nan_path_t *path);
bool esp32_mquickjs_wifi_nan_path_retain(esp32_mquickjs_wifi_nan_path_t *path);
void esp32_mquickjs_wifi_nan_path_release(esp32_mquickjs_wifi_nan_path_t *path);
void esp32_mquickjs_wifi_nan_path_close(esp32_mquickjs_wifi_nan_path_t *path, bool timeout);
void esp32_mquickjs_wifi_nan_path_status(esp32_mquickjs_wifi_nan_path_t *path,
    esp32_mquickjs_wifi_nan_path_status_t *status);
esp_err_t esp32_mquickjs_wifi_nan_path_respond(esp32_mquickjs_wifi_nan_path_t *path, bool accept,
    const uint8_t *ssi, uint16_t length);
/* Receive reserves an exact native owner until JS construction succeeds.
 * Failed conversion/cancel unreserves it; success transfers that reference. */
esp_err_t esp32_mquickjs_wifi_nan_path_receive(esp32_mquickjs_wifi_nan_discovery_t *service,
    esp32_mquickjs_wifi_nan_path_t **path);
bool esp32_mquickjs_wifi_nan_path_deliver(esp32_mquickjs_wifi_nan_path_t *path);
void esp32_mquickjs_wifi_nan_path_unreserve(esp32_mquickjs_wifi_nan_path_t *path);
#endif
