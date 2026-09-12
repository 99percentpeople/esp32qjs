#pragma once
#include "esp32_mquickjs_wifi_nan_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#define ESP32_MQUICKJS_NAN_MAX_HANDLES 4U
#define ESP32_MQUICKJS_NAN_MAX_STARTUP_MS 120000U
typedef struct esp32_mquickjs_wifi_nan_session esp32_mquickjs_wifi_nan_session_t;
typedef struct {
    uint32_t identity;
    esp32_mquickjs_wifi_nan_radio_status_t native;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
    size_t reserved_bytes;
    bool activated, worker_busy, ready, closing, timed_out, retired;
    bool native_start_seen, native_stop_seen;
    bool group_management_protection, usd;
} esp32_mquickjs_wifi_nan_session_status_t;
typedef struct {
    esp32_mquickjs_wifi_nan_session_status_t session;
    uint32_t handles, workers;
    uint32_t service_handles, active_services, message_handles;
    uint32_t path_handles, active_paths;
    uint32_t pairing_handles, active_pairings;
    bool active, runtime_closing;
} esp32_mquickjs_wifi_nan_global_status_t;

esp_err_t esp32_mquickjs_wifi_nan_open_runtime(void);
void esp32_mquickjs_wifi_nan_global_status(esp32_mquickjs_wifi_nan_global_status_t *status);
/* Construction owns only native storage. Runtime activation follows complete
 * JS handle construction; finalization may request close without holding JS
 * roots. Native registry/worker references persist until exact Radio release. */
esp_err_t esp32_mquickjs_wifi_nan_session_create(const wifi_nan_sync_config_t *config,
    uint32_t timeout_ms, esp32_mquickjs_wifi_nan_session_t **out);
esp_err_t esp32_mquickjs_wifi_nan_session_create_usd(uint32_t timeout_ms,
    esp32_mquickjs_wifi_nan_session_t **out);
esp_err_t esp32_mquickjs_wifi_nan_session_activate(esp32_mquickjs_wifi_nan_session_t *session);
bool esp32_mquickjs_wifi_nan_session_retain(esp32_mquickjs_wifi_nan_session_t *session);
void esp32_mquickjs_wifi_nan_session_release(esp32_mquickjs_wifi_nan_session_t *session);
void esp32_mquickjs_wifi_nan_session_close(esp32_mquickjs_wifi_nan_session_t *session, bool timeout);
bool esp32_mquickjs_wifi_nan_session_status(esp32_mquickjs_wifi_nan_session_t *session,
    esp32_mquickjs_wifi_nan_session_status_t *status);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
/* Caller retains the Session through this bounded read. Never hold the Session
 * critical section while taking Radio/NAN locks. The result owns only copies. */
esp_err_t esp32_mquickjs_wifi_nan_session_query(esp32_mquickjs_wifi_nan_session_t *session,
    const esp32_mquickjs_wifi_nan_query_t *query, esp32_mquickjs_wifi_nan_query_result_t *out);
#endif
bool esp32_mquickjs_wifi_nan_service(void);
bool esp32_mquickjs_wifi_nan_prepare_runtime_destroy(void);
#endif
