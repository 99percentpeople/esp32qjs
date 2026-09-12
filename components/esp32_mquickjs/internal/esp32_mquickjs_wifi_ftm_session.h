#pragma once
#include "esp32_mquickjs_wifi_ftm_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
#define ESP32_MQUICKJS_WIFI_FTM_MAX_HANDLES 8U
#define ESP32_MQUICKJS_WIFI_FTM_MAX_RETAINED_ENTRIES 256U

typedef struct esp32_mquickjs_wifi_ftm_session esp32_mquickjs_wifi_ftm_session_t;
typedef struct {
    wifi_ftm_initiator_cfg_t config;
    esp32_mquickjs_wifi_ftm_state_t native;
    uint16_t report_capacity, retained_entries;
    bool started, submitted, end_requested, close_requested, retired, report_ready;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
} esp32_mquickjs_wifi_ftm_status_t;
typedef struct {
    uint32_t handles, reserved_entries;
    bool active, worker_busy, cleanup_pending;
} esp32_mquickjs_wifi_ftm_counts_t;

/* Native references only. Creation performs no SDK I/O; both allocations are
 * completed before start. capacity 0 chooses summary only; otherwise 1..64.
 * All open/closed/retired handles share the 8-handle budget; at most 256 detailed
 * entries are reserved across them. Closed handles retain status, not reports.
 * The active registry and queued worker own independent references. */
esp_err_t esp32_mquickjs_wifi_ftm_create(const wifi_ftm_initiator_cfg_t *config,
    unsigned capacity, esp32_mquickjs_wifi_ftm_session_t **output);
bool esp32_mquickjs_wifi_ftm_retain(esp32_mquickjs_wifi_ftm_session_t *session);
void esp32_mquickjs_wifi_ftm_release(esp32_mquickjs_wifi_ftm_session_t *session);
esp_err_t esp32_mquickjs_wifi_ftm_start(esp32_mquickjs_wifi_ftm_session_t *session);
/* end preserves a subsequent report. close prevents report access immediately,
 * drops native report storage once the worker stops using it, and keeps cleanup
 * responsibility until Radio retirement. Neither is proof of native termination. */
void esp32_mquickjs_wifi_ftm_end(esp32_mquickjs_wifi_ftm_session_t *session);
void esp32_mquickjs_wifi_ftm_close(esp32_mquickjs_wifi_ftm_session_t *session);
bool esp32_mquickjs_wifi_ftm_status(esp32_mquickjs_wifi_ftm_session_t *session,
    esp32_mquickjs_wifi_ftm_status_t *output);
/* Copies a single immutable native entry only after exact Radio retirement.
 * Caller retains the session. No SDK, allocation, pointer borrowing or lossy
 * timestamp conversion; output stays unchanged on failure. */
bool esp32_mquickjs_wifi_ftm_report_entry(esp32_mquickjs_wifi_ftm_session_t *session,
    unsigned index, wifi_ftm_report_entry_t *output);
bool esp32_mquickjs_wifi_ftm_service(void);
bool esp32_mquickjs_wifi_ftm_prepare_runtime_destroy(void);
bool esp32_mquickjs_wifi_ftm_current_status(esp32_mquickjs_wifi_ftm_status_t *output);
void esp32_mquickjs_wifi_ftm_counts(esp32_mquickjs_wifi_ftm_counts_t *output);
#endif
