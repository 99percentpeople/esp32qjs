#pragma once
#include "esp32_mquickjs_wifi_mesh_radio.h"
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
#define ESP32_MQUICKJS_MESH_HANDLES 4U
#define ESP32_MQUICKJS_MESH_JOBS 8U
#define ESP32_MQUICKJS_MESH_MAX_WAIT_MS 120000U
typedef struct esp32_mquickjs_wifi_mesh_session esp32_mquickjs_wifi_mesh_session_t;
typedef struct esp32_mquickjs_wifi_mesh_job esp32_mquickjs_wifi_mesh_job_t;
typedef struct {
    esp32_mquickjs_wifi_mesh_radio_status_t native;
    uint32_t identity, reserved_bytes, recovery_attempts;
    esp_err_t error, cleanup_error, receive_error[2];
    const char *stage;
    bool activated, worker_busy, ready, closing, retired, timed_out, recovery_pending;
} esp32_mquickjs_wifi_mesh_session_status_t;
typedef struct {
    uint32_t identity, bytes;
    esp_err_t error, native_error;
    int64_t completed_us;
    uint16_t count;
    bool done, cancelled, dispatched, returned, timed_out;
} esp32_mquickjs_wifi_mesh_job_status_t;
esp_err_t esp32_mquickjs_wifi_mesh_open_runtime(void);
esp_err_t esp32_mquickjs_wifi_mesh_session_create(const esp32_mquickjs_wifi_mesh_config_t *config,
    bool allow_ap_restart, uint32_t timeout_ms, esp32_mquickjs_wifi_mesh_session_t **output);
esp_err_t esp32_mquickjs_wifi_mesh_session_activate(esp32_mquickjs_wifi_mesh_session_t *session);
bool esp32_mquickjs_wifi_mesh_session_retain(esp32_mquickjs_wifi_mesh_session_t *session);
void esp32_mquickjs_wifi_mesh_session_release(esp32_mquickjs_wifi_mesh_session_t *session);
void esp32_mquickjs_wifi_mesh_session_close(esp32_mquickjs_wifi_mesh_session_t *session, bool timeout);
esp_err_t esp32_mquickjs_wifi_mesh_session_recover(esp32_mquickjs_wifi_mesh_session_t *session);
void esp32_mquickjs_wifi_mesh_session_status(esp32_mquickjs_wifi_mesh_session_t *session,
    esp32_mquickjs_wifi_mesh_session_status_t *status);
/* A job owns copied native data and a parent reference. Dispatch gives the
 * Session another job reference, retained even when a Future is cancelled. */
esp_err_t esp32_mquickjs_wifi_mesh_job_create(esp32_mquickjs_wifi_mesh_session_t *session,
    const esp32_mquickjs_wifi_mesh_send_t *send, const esp32_mquickjs_wifi_mesh_control_t *control,
    uint32_t timeout_ms, esp32_mquickjs_wifi_mesh_job_t **output);
esp_err_t esp32_mquickjs_wifi_mesh_job_submit(esp32_mquickjs_wifi_mesh_job_t *job);
void esp32_mquickjs_wifi_mesh_job_cancel(esp32_mquickjs_wifi_mesh_job_t *job);
void esp32_mquickjs_wifi_mesh_job_release(esp32_mquickjs_wifi_mesh_job_t *job);
void esp32_mquickjs_wifi_mesh_job_result(esp32_mquickjs_wifi_mesh_job_t *job,
    esp32_mquickjs_wifi_mesh_job_status_t *status, const mesh_addr_t **addresses);
/* Caller retains the job; NULL until the worker has published its completion. */
const esp32_mquickjs_wifi_mesh_control_data_t *esp32_mquickjs_wifi_mesh_job_detail(
    esp32_mquickjs_wifi_mesh_job_t *job);
esp_err_t esp32_mquickjs_wifi_mesh_read_claim(esp32_mquickjs_wifi_mesh_session_t *session,
    bool to_ds, uint32_t *identity);
void esp32_mquickjs_wifi_mesh_read_release(esp32_mquickjs_wifi_mesh_session_t *session,
    bool to_ds, uint32_t identity);
esp_err_t esp32_mquickjs_wifi_mesh_message_copy(esp32_mquickjs_wifi_mesh_session_t *session,
    bool to_ds, uint32_t identity, esp32_mquickjs_wifi_mesh_message_t *message, uint8_t *bytes, size_t capacity);
esp_err_t esp32_mquickjs_wifi_mesh_message_commit(esp32_mquickjs_wifi_mesh_session_t *session,
    bool to_ds, uint32_t identity, uint32_t sequence);
esp_err_t esp32_mquickjs_wifi_mesh_event_copy(esp32_mquickjs_wifi_mesh_session_t *session,
    esp32_mquickjs_wifi_mesh_notice_t *notice);
esp_err_t esp32_mquickjs_wifi_mesh_event_commit(esp32_mquickjs_wifi_mesh_session_t *session, uint32_t sequence);
bool esp32_mquickjs_wifi_mesh_service(void);
bool esp32_mquickjs_wifi_mesh_prepare_runtime_destroy(void);
esp_err_t esp32_mquickjs_wifi_mesh_scan_read_claim(esp32_mquickjs_wifi_mesh_session_t *session, uint32_t *identity);
void esp32_mquickjs_wifi_mesh_scan_read_release(esp32_mquickjs_wifi_mesh_session_t *session, uint32_t identity);
esp_err_t esp32_mquickjs_wifi_mesh_scan_commit(esp32_mquickjs_wifi_mesh_session_t *session,
    uint32_t read_identity, uint32_t scan_identity, uint32_t sequence);
#endif
