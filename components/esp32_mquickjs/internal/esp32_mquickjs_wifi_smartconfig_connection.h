#pragma once
#include "esp32_mquickjs_wifi_smartconfig_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
typedef struct {
    uint32_t generation, terminal;
    int32_t reason;
    bool connected, draining;
} esp32_mquickjs_wifi_smartconfig_connection_status_t;

/* Runtime task only. Borrow the exact SmartConfig operation after capture has
 * stopped. Native events use the existing Station helper and generation; they
 * do not fabricate a Future token or depend on an observation queue. */
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_begin(
    const esp32_mquickjs_wifi_radio_operation_t *owner, const wifi_config_t *config,
    uint32_t timeout_ms, uint32_t *generation);
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_status(
    const esp32_mquickjs_wifi_radio_operation_t *owner, uint32_t generation,
    esp32_mquickjs_wifi_smartconfig_connection_status_t *status);
/* keep_connected hands a successful connection to the application. Otherwise
 * disconnect only this generation and wait for its existing IP/timer fences.
 * An error retains the native registration and Radio borrow. */
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_end(
    const esp32_mquickjs_wifi_radio_operation_t *owner, uint32_t generation,
    bool keep_connected);
#endif
