#pragma once
#include "esp32_mquickjs_wifi_wps_station.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#define ESP32_MQUICKJS_WPS_MAX_HANDLES 4U
typedef struct esp32_mquickjs_wifi_wps_session esp32_mquickjs_wifi_wps_session_t;
typedef struct {
    esp32_mquickjs_wifi_wps_radio_status_t native;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
    size_t reserved_bytes;
    bool activated, worker_busy, pin_ready, pin_consumed;
    bool credentials_ready, credentials_consumed, closing, retired, timed_out;
    bool capture_finished, native_closed, helper_drained;
} esp32_mquickjs_wifi_wps_session_status_t;
typedef struct {
    unsigned handles, workers;
    bool active, runtime_closing;
    esp32_mquickjs_wifi_wps_session_status_t session;
} esp32_mquickjs_wifi_wps_global_status_t;

esp_err_t esp32_mquickjs_wifi_wps_open_runtime(void);
esp_err_t esp32_mquickjs_wifi_wps_session_create(const esp_wps_config_t *config,
    bool allow_ap_channel_change, uint32_t timeout_ms, esp32_mquickjs_wifi_wps_session_t **out);
/* Runtime task only; activate after the JS handle has been constructed. */
esp_err_t esp32_mquickjs_wifi_wps_session_activate(esp32_mquickjs_wifi_wps_session_t *session);
bool esp32_mquickjs_wifi_wps_session_retain(esp32_mquickjs_wifi_wps_session_t *session);
void esp32_mquickjs_wifi_wps_session_release(esp32_mquickjs_wifi_wps_session_t *session);
void esp32_mquickjs_wifi_wps_session_close(esp32_mquickjs_wifi_wps_session_t *session, bool timeout);
bool esp32_mquickjs_wifi_wps_session_status(esp32_mquickjs_wifi_wps_session_t *session,
    esp32_mquickjs_wifi_wps_session_status_t *status);
void esp32_mquickjs_wifi_wps_global_status(esp32_mquickjs_wifi_wps_global_status_t *status);
/* Native copies only, no SDK work on the JS thread. Commit only after successful
 * conversion. Worker writes exclude readers; a public Future doesn't own the
 * native worker or cancel an in-flight dispatch when it ends. */
esp_err_t esp32_mquickjs_wifi_wps_session_pin(esp32_mquickjs_wifi_wps_session_t *session,
    uint8_t pin[8], bool commit);
esp_err_t esp32_mquickjs_wifi_wps_session_credentials(esp32_mquickjs_wifi_wps_session_t *session,
    esp32_mquickjs_wifi_wps_credentials_t *credentials, bool commit);
bool esp32_mquickjs_wifi_wps_session_wait_begin(esp32_mquickjs_wifi_wps_session_t *session, bool close);
void esp32_mquickjs_wifi_wps_session_wait_end(esp32_mquickjs_wifi_wps_session_t *session, bool close);
bool esp32_mquickjs_wifi_wps_session_observation(esp32_mquickjs_wifi_wps_session_t *session,
    esp32_mquickjs_wifi_wps_session_status_t *status);
bool esp32_mquickjs_wifi_wps_service(void);
bool esp32_mquickjs_wifi_wps_prepare_runtime_destroy(void);
#endif
