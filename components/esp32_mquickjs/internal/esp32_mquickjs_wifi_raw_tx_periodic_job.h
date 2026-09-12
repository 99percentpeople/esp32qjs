#pragma once
#include "esp32_mquickjs_wifi_raw_tx_periodic.h"
#include "esp32_mquickjs_wifi_raw_tx_session.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#define ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS 8U
typedef struct esp32_mquickjs_wifi_raw_tx_periodic_job esp32_mquickjs_wifi_raw_tx_periodic_job_t;
typedef struct {
    esp32_mquickjs_wifi_raw_tx_periodic_t ledger;
    bool ready, retired, stop_requested, close_requested, worker_busy, timer_present, timer_quiesced, timer_transition;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
} esp32_mquickjs_wifi_raw_tx_periodic_job_status_t;
typedef struct {
    uint8_t live, retired, faulted, cleanup_pending;
    bool identity_exhausted;
    uint32_t error_generation;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
} esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t;
void esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t *output);
/* Create storage/reserve a Session child; no timer or Radio I/O. Success takes
 * template ownership and returns one caller reference. Failure changes neither
 * frame nor output. Session options and caller frame must be stable for the call.
 * Service starts the timer on a native worker; ready/error describe actual start.
 * At most 8 jobs, including retired jobs retained by callers; fresh boot identities
 * never wrap. Caller requests close before releasing its final public owner. */
esp_err_t esp32_mquickjs_wifi_raw_tx_periodic_job_new(
    esp32_mquickjs_wifi_raw_tx_session_t *session, esp32_mquickjs_wifi_raw_tx_payload_t *frame,
    const esp32_mquickjs_wifi_raw_tx_periodic_options_t *options,
    esp32_mquickjs_wifi_raw_tx_periodic_job_t **output);
bool esp32_mquickjs_wifi_raw_tx_periodic_job_retain(esp32_mquickjs_wifi_raw_tx_periodic_job_t *job);
void esp32_mquickjs_wifi_raw_tx_periodic_job_release(esp32_mquickjs_wifi_raw_tx_periodic_job_t *job);
void esp32_mquickjs_wifi_raw_tx_periodic_job_stop(esp32_mquickjs_wifi_raw_tx_periodic_job_t *job);
void esp32_mquickjs_wifi_raw_tx_periodic_job_close(esp32_mquickjs_wifi_raw_tx_periodic_job_t *job);
bool esp32_mquickjs_wifi_raw_tx_periodic_job_status(esp32_mquickjs_wifi_raw_tx_periodic_job_t *job,
    esp32_mquickjs_wifi_raw_tx_periodic_job_status_t *output);
/* Poller handles initial admission and cleanup retries after a disarmed timer.
 * Once started, timer wakeups and native workers advance Session sends without
 * JS cooperation. No JS/runtime pointer is retained by this module. */
bool esp32_mquickjs_wifi_raw_tx_periodic_jobs_service(void);
#endif
