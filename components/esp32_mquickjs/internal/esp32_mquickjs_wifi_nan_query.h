#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdint.h>

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#include "esp_nan.h"
typedef enum {
    ESP32_MQUICKJS_NAN_QUERY_SERVICE,
    ESP32_MQUICKJS_NAN_QUERY_PEER,
    ESP32_MQUICKJS_NAN_QUERY_PEERS,
} esp32_mquickjs_wifi_nan_query_kind_t;
typedef struct {
    esp32_mquickjs_wifi_nan_query_kind_t kind;
    /* Exactly one selector, except PEER may search every current service.
     * Names are borrowed only during the call; IDs select current native cache
     * entries and never grant service ownership or authorize a mutation. */
    uint8_t service_id, peer[6];
    const char *service_name;
} esp32_mquickjs_wifi_nan_query_t;
typedef struct {
    uint8_t service_id, peer_count;
    char service_name[ESP_WIFI_MAX_SVC_NAME_LEN];
    struct nan_peer_record peers[NAN_MAX_PEERS_RECORD];
} esp32_mquickjs_wifi_nan_query_result_t;

/* Caller owns the exact active Sync Radio token. Copies bounded metadata
 * under one native data lock, with no allocation, ioctl, observer or JS call.
 * All output is cleared on failure, including NOT_FOUND. No keys or native
 * pointers escape. PEER writes peers[0]; PEERS writes peer_count records. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_query(const esp32_mquickjs_wifi_nan_query_t *query,
    esp32_mquickjs_wifi_nan_query_result_t *out);
#endif
