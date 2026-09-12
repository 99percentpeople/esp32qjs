#pragma once
#include "esp32_mquickjs_wifi_rrm_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_RRM_SUPPORT
#define ESP32_MQUICKJS_WIFI_RRM_MAX_HANDLES 8U
#define ESP32_MQUICKJS_WIFI_RRM_MAX_REPORT_BYTES 4096U
#define ESP32_MQUICKJS_WIFI_RRM_MAX_RETAINED_BYTES 16384U
#define ESP32_MQUICKJS_WIFI_RRM_COPY_BYTES 256U
typedef struct esp32_mquickjs_wifi_rrm_request esp32_mquickjs_wifi_rrm_request_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RRM_PENDING,
    ESP32_MQUICKJS_WIFI_RRM_REPORT,
    ESP32_MQUICKJS_WIFI_RRM_NO_REPORT, /* SDK timeout/reset are indistinguishable. */
    ESP32_MQUICKJS_WIFI_RRM_FAILED,
    ESP32_MQUICKJS_WIFI_RRM_CANCELLED,
    ESP32_MQUICKJS_WIFI_RRM_TIMED_OUT, /* Explicit local deadline, not native retirement. */
} esp32_mquickjs_wifi_rrm_terminal_t;
typedef struct {
    esp32_mquickjs_wifi_radio_operation_t operation; /* Historical, retained after retirement. */
    esp32_mquickjs_wifi_rrm_terminal_t terminal;
    esp32_mquickjs_wifi_rrm_sdk_result_t submit, cancel, cleanup;
    esp_err_t error, cancel_error, cleanup_error;
    const char *stage, *cleanup_stage;
    size_t received_bytes;
    uint16_t capacity, retained_bytes;
    bool started, worker_started, callback_seen, cancel_written, closed, retired;
} esp32_mquickjs_wifi_rrm_status_t;
typedef struct {
    uint32_t handles, reserved_bytes;
    bool active, worker_busy, callback_busy;
} esp32_mquickjs_wifi_rrm_counts_t;

/* Native only. Reserve all memory before SDK I/O. At most one active request;
 * completed handles retain reports within the shared handle/byte budgets.
 * Caller must own a reference for every access; finalizers close then release. */
esp_err_t esp32_mquickjs_wifi_rrm_create(unsigned capacity, esp32_mquickjs_wifi_rrm_request_t **output);
bool esp32_mquickjs_wifi_rrm_retain(esp32_mquickjs_wifi_rrm_request_t *request);
void esp32_mquickjs_wifi_rrm_release(esp32_mquickjs_wifi_rrm_request_t *request);
esp_err_t esp32_mquickjs_wifi_rrm_start(esp32_mquickjs_wifi_rrm_request_t *request);
void esp32_mquickjs_wifi_rrm_cancel(esp32_mquickjs_wifi_rrm_request_t *request, bool timeout);
void esp32_mquickjs_wifi_rrm_close(esp32_mquickjs_wifi_rrm_request_t *request);
bool esp32_mquickjs_wifi_rrm_status(esp32_mquickjs_wifi_rrm_request_t *request,
    esp32_mquickjs_wifi_rrm_status_t *output);
/* Immutable chunks, only after retirement, at most COPY_BYTES per call.
 * No borrowed pointer and no partial copy on error. Close revokes access. */
bool esp32_mquickjs_wifi_rrm_copy(esp32_mquickjs_wifi_rrm_request_t *request,
    size_t offset, void *output, size_t length);
bool esp32_mquickjs_wifi_rrm_service(void);
bool esp32_mquickjs_wifi_rrm_prepare_runtime_destroy(void);
void esp32_mquickjs_wifi_rrm_counts(esp32_mquickjs_wifi_rrm_counts_t *output);
bool esp32_mquickjs_wifi_rrm_current_status(esp32_mquickjs_wifi_rrm_status_t *output);
/* Future capture/destroy bracket every pending public operation, including
 * OPEN. Caller already retains the request. No observer while waiters exist. */
bool esp32_mquickjs_wifi_rrm_waiter_add(esp32_mquickjs_wifi_rrm_request_t *request);
void esp32_mquickjs_wifi_rrm_waiter_remove(esp32_mquickjs_wifi_rrm_request_t *request);
/* Runtime poller only, after Future polling has returned. Never call from a
 * worker or driver finish/destroy. discard drains during runtime teardown. */
bool esp32_mquickjs_wifi_rrm_poll_observations(bool discard);
#endif
