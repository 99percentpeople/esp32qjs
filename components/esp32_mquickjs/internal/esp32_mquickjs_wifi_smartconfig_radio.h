#pragma once
#include "esp32_mquickjs_wifi_smartconfig_decoder.h"
#include "esp32_mquickjs_wifi_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
typedef struct {
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp32_mquickjs_wifi_smartconfig_decoder_status_t decoder;
    esp32_mquickjs_wifi_smartconfig_events_status_t events;
    esp_err_t error;
    const char *stage;
    uint8_t home_primary;
    wifi_second_chan_t home_secondary;
    bool channel_restored, event_fenced;
} esp32_mquickjs_wifi_smartconfig_radio_status_t;

/* Worker calls. Exact helper owners are pinned through successful native close
 * AND home-channel restoration. Requires disconnected, started 2.4 GHz STA;
 * APSTA additionally requires explicit consent for AP channel interruption.
 * Output token on failure is a retained cleanup duty, not a successful start. */
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_begin(
    const esp32_mquickjs_wifi_radio_lease_t owners[3], bool allow_ap_channel_change,
    const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,
    esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_ack(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status);
/* One exact Station connection borrow; ordinary CONNECT admission stays closed.
 * All helper leases and the SmartConfig operation remain pinned through ACK. */
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_connection_begin(const esp32_mquickjs_wifi_radio_operation_t *token);
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_connection_end(const esp32_mquickjs_wifi_radio_operation_t *token);
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status);
/* Credential storage remains in the event owner. Caller scrubs copies; commit
 * is separate so JS conversion failure never silently consumes credentials. */
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_credentials_t *credentials, bool commit);
/* Runtime task only, before scheduling a worker. Values are copied and Radio
 * revalidates their exact identities; no mutable helper pointers escape. */
esp_err_t esp32_mquickjs_wifi_smartconfig_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3]);
#endif
