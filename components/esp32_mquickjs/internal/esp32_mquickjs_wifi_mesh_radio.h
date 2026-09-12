#pragma once
#include "esp32_mquickjs_wifi_radio.h"

#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
#include "esp_netif.h"

/* The lifecycle identity survives the physical driver rebuild used to restore
 * its predecessor. The native Mesh token remains private to this binding. */
typedef esp32_mquickjs_wifi_radio_lifecycle_t esp32_mquickjs_wifi_mesh_radio_token_t;
typedef struct {
    esp32_mquickjs_wifi_mesh_radio_token_t token;
    esp32_mquickjs_wifi_mesh_status_t native;
    const char *stage, *cleanup_stage;
    esp_err_t error, cleanup_error, network_error;
    wifi_mode_t original_mode;
    uint32_t reserved_bytes;
    bool cold, ready, closing, busy, checkpoint_complete, stopped, netifs_retired;
    bool restored, restart_required, dhcp_started, ip_ready;
    esp_netif_ip_info_t ip;
} esp32_mquickjs_wifi_mesh_radio_status_t;

/* Worker only. Admit healthy zero-owner STOP or cold initialization, with no
 * foreign default STA/AP netifs. allow_ap_restart acknowledges that restoring
 * an original AP mode transiently starts that AP before returning to STOP.
 * A nonzero output token on failure transfers cleanup duty to the caller.
 * SDK blocking calls release the Radio mutex but retain the exact binding. */
esp_err_t esp32_mquickjs_wifi_radio_mesh_begin(const esp32_mquickjs_wifi_mesh_config_t *config,
    bool allow_ap_restart, esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_mesh_close(esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status);
/* Explicit retry of a failed restoration, using the original frozen config.
 * Never retries an uncertain native Mesh init/deinit or clears foreign faults. */
esp_err_t esp32_mquickjs_wifi_radio_mesh_recover(esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_mesh_poll(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status);
/* Nonblocking snapshot: TIMEOUT means the caller should keep its previous
 * cached observation. Does not run SDK getters or enter a JS wait scope. */
esp_err_t esp32_mquickjs_wifi_radio_mesh_status(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status);
esp_err_t esp32_mquickjs_wifi_radio_mesh_send(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    const esp32_mquickjs_wifi_mesh_send_t *send);
esp_err_t esp32_mquickjs_wifi_radio_mesh_control(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_control_t *control);
esp_err_t esp32_mquickjs_wifi_radio_mesh_receive(const esp32_mquickjs_wifi_mesh_radio_token_t *token, bool to_ds);
esp_err_t esp32_mquickjs_wifi_radio_mesh_message_copy(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    bool to_ds, esp32_mquickjs_wifi_mesh_message_t *message, uint8_t *bytes, size_t capacity);
esp_err_t esp32_mquickjs_wifi_radio_mesh_message_commit(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    bool to_ds, uint32_t sequence);
esp_err_t esp32_mquickjs_wifi_radio_mesh_event_copy(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_notice_t *notice);
esp_err_t esp32_mquickjs_wifi_radio_mesh_event_commit(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    uint32_t sequence);
esp_err_t esp32_mquickjs_wifi_radio_mesh_scan_commit(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    uint32_t scan_identity, uint32_t sequence);
#endif
