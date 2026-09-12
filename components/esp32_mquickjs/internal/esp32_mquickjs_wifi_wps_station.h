#pragma once
#include "esp32_mquickjs_wifi_wps_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
/* Runtime task only. Reserve before scheduling the first Radio worker. This
 * blocks public connect/scan and helper teardown during asynchronous admission.
 * No WPS driver mutation occurs here. Identity never wraps or gets reset by a
 * runtime restart. Call drain only AFTER Radio reports native retirement and
 * its boot event fence; call release only after final Radio close/no admission. */
esp_err_t esp32_mquickjs_wifi_wps_station_reserve(uint32_t *identity,
    esp32_mquickjs_wifi_radio_lease_t owners[3]);
esp_err_t esp32_mquickjs_wifi_wps_station_drain(uint32_t identity);
esp_err_t esp32_mquickjs_wifi_wps_station_release(uint32_t *identity);
#endif
