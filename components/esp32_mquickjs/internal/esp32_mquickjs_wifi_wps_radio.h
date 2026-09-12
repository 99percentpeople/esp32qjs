#pragma once
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wifi_wps_worker.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
typedef struct {
    esp32_mquickjs_wifi_radio_operation_t operation;
    esp32_mquickjs_wifi_wps_worker_status_t worker;
    esp_err_t error;
    const char *stage;
    size_t reserved_bytes;
    bool config_restored, channel_restored, storage_restored, event_fenced;
} esp32_mquickjs_wifi_wps_radio_status_t;

/* Serialized background worker entry points; no JS/ISR/native callback caller.
 * Requires disconnected STARTED Station and exact helper lease snapshots.
 * APSTA requires explicit consent for channel interruption. All helper owners
 * stay pinned until native cleanup, config/channel/storage restore and the boot
 * event fence complete. Failure with a nonzero token transfers cleanup duty. */
esp_err_t esp32_mquickjs_wifi_radio_wps_begin(
    const esp32_mquickjs_wifi_radio_lease_t owners[3], bool allow_ap_channel_change,
    const esp_wps_config_t *config, esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_wps_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_wps_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status);
/* Discard native results and stop/restore/fence, but retain the Radio binding.
 * Session must drain its Station helper after this, before final close. */
esp_err_t esp32_mquickjs_wifi_radio_wps_prepare_close(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_wps_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status);
/* PIN available during negotiation. Credentials only after complete capture
 * retirement/restoration/event fence. Successful conversion precedes commit. */
esp_err_t esp32_mquickjs_wifi_radio_wps_pin(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t pin[8], bool commit);
esp_err_t esp32_mquickjs_wifi_radio_wps_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_credentials_t *credentials, bool commit);
#endif
