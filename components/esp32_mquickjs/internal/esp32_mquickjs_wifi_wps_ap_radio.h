#pragma once
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wifi_wps_ap_worker.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
typedef struct {
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp32_mquickjs_wifi_wps_ap_worker_status_t worker;
    esp_err_t error;
    const char *stage;
    size_t reserved_bytes;
    bool event_fenced;
} esp32_mquickjs_wifi_wps_ap_radio_status_t;

/* Serialized background calls. Existing AP configuration/channel remain owned
 * by their helpers; no STA disconnect, scan, storage change or implicit start.
 * A nonzero output token retains every supplied lease across failed admission. */
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],
    const esp_wps_config_t *config, esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_pin(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t pin[8], bool commit);

/* Runtime-only helper pre-reservation. Release AFTER final Radio close (or
 * rejected admission). It never tears down AP clients or the STA connection. */
esp_err_t esp32_mquickjs_wifi_wps_ap_helper_reserve(uint32_t *identity,
    esp32_mquickjs_wifi_radio_lease_t owners[3]);
esp_err_t esp32_mquickjs_wifi_wps_ap_helper_release(uint32_t *identity);
bool esp32_mquickjs_wifi_wps_ap_helper_held(void);
#endif
