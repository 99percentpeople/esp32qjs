#pragma once
#include "esp32_mquickjs_wifi_dpp_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_types.h"
typedef struct {
    uint32_t generation, terminal;
    int32_t reason;
    bool connected, draining;
    esp32_mquickjs_wifi_link_snapshot_t link;
} esp32_mquickjs_wifi_dpp_connection_status_t;
/* Runtime-only admission/drain, sharing the existing Station capture slot
 * with WPS. Reserve before scheduling Radio; release after native/Radio close. */
esp_err_t esp32_mquickjs_wifi_dpp_station_reserve(uint32_t *identity,
    esp32_mquickjs_wifi_radio_lease_t owners[3]);
esp_err_t esp32_mquickjs_wifi_dpp_station_drain(uint32_t identity);
esp_err_t esp32_mquickjs_wifi_dpp_station_release(uint32_t *identity);
esp_err_t esp32_mquickjs_wifi_dpp_connect_begin(uint32_t capture_identity,
    const esp32_mquickjs_wifi_radio_operation_t *owner, const wifi_config_t *config,
    uint32_t timeout_ms, uint32_t *generation);
esp_err_t esp32_mquickjs_wifi_dpp_connect_status(const esp32_mquickjs_wifi_radio_operation_t *owner,
    uint32_t generation, esp32_mquickjs_wifi_dpp_connection_status_t *status);
esp_err_t esp32_mquickjs_wifi_dpp_connect_end(const esp32_mquickjs_wifi_radio_operation_t *owner,
    uint32_t generation);
#endif
