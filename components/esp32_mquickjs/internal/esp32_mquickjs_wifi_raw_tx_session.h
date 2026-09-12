#pragma once
#include "esp32_mquickjs_wifi_raw_tx_broker.h"
#include "esp32_mquickjs_wifi_raw_tx_queue.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_tx_rate.h"
#define ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS 8U
#define ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS 8U
typedef struct esp32_mquickjs_wifi_raw_tx_session esp32_mquickjs_wifi_raw_tx_session_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_PENDING,
    ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_COMPLETED,
    ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_REJECTED,
    ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_DROPPED,
    ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_ABORTED,
    ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_UNCERTAIN,
    /* Native teardown proven after physical deinit, never a driver completion. */
    ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_TERMINATED,
} esp32_mquickjs_wifi_raw_tx_result_kind_t;
typedef struct { uint32_t generation, identity; uint8_t index; } esp32_mquickjs_wifi_raw_tx_result_token_t;
typedef struct {
    uint32_t generation, sequence;
    esp32_mquickjs_wifi_raw_tx_result_kind_t kind;
    esp_err_t error;
    const char *stage;
    uint8_t channel;
    bool driver_submitted;
    esp32_mquickjs_wifi_raw_tx_broker_status_t native;
} esp32_mquickjs_wifi_raw_tx_result_t;
typedef struct {
    /* Caller-owned stable native storage, not a driver callback argument.
     * Only the Session mutex may access these fields while registered. */
    bool registered;
    uint32_t identity;
    const uint8_t *payload;
    esp32_mquickjs_wifi_raw_tx_result_t result;
} esp32_mquickjs_wifi_raw_tx_result_record_t;
typedef struct {
    esp32_mquickjs_wifi_raw_tx_interface_t interface;
    uint8_t channel;
    bool driver_sequence;
    bool rate_set;
    wifi_tx_rate_config_t rate;
    uint16_t capacity;
    esp32_mquickjs_wifi_raw_tx_queue_overflow_t overflow;
} esp32_mquickjs_wifi_raw_tx_session_options_t;
typedef struct {
    uint32_t generation, radio_generation, active_sequence, lane_identity;
    uint8_t channel;
    uint16_t queued, capacity;
    uint8_t periodic_children;
    bool open_complete, close_requested, closed, faulted, worker_busy;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
    esp32_mquickjs_wifi_raw_tx_queue_totals_t totals;
    uint32_t last_sequence;
    esp32_mquickjs_wifi_raw_tx_broker_status_t last_completion;
} esp32_mquickjs_wifi_raw_tx_session_status_t;
typedef struct {
    uint8_t live, closed, closing, faulted, pending_results, pending_flushes;
    uint32_t error_generation;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
} esp32_mquickjs_wifi_raw_tx_sessions_status_t;

/* Creates native storage only, returning one owned reference. The existing
 * Raw TX service schedules Radio opening on a runtime-free background worker.
 * All internal controls are bounded by MAX_SESSIONS, including closed Sessions
 * still retained by callers/flush watchers. No JS or runtime pointer is stored.
 * A successful new is not successful Radio opening: inspect open_complete/error.
 * Input options and captured payloads must be stable throughout each call. */
esp_err_t esp32_mquickjs_wifi_raw_tx_session_new(
    const esp32_mquickjs_wifi_raw_tx_session_options_t *options,
    esp32_mquickjs_wifi_raw_tx_session_t **output);
/* Only retain an already-owned pointer. The final JS owner must request_close
 * BEFORE release; close itself never releases the caller's reference. */
bool esp32_mquickjs_wifi_raw_tx_session_retain(esp32_mquickjs_wifi_raw_tx_session_t *session);
void esp32_mquickjs_wifi_raw_tx_session_release(esp32_mquickjs_wifi_raw_tx_session_t *session);
void esp32_mquickjs_wifi_raw_tx_session_request_close(esp32_mquickjs_wifi_raw_tx_session_t *session);
bool esp32_mquickjs_wifi_raw_tx_session_status(esp32_mquickjs_wifi_raw_tx_session_t *session,
    esp32_mquickjs_wifi_raw_tx_session_status_t *output);
/* Captures no JS. Validate all uniquely-owned payloads before atomic queue
 * admission. On success inputs move to queue; evictions move to the caller's
 * empty removed[capacity] array for freeing OUTSIDE all locks. On failure the
 * inputs/queue/admission remain unchanged; validation may report the rejection. */
esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_session_admit(
    esp32_mquickjs_wifi_raw_tx_session_t *session, esp32_mquickjs_wifi_raw_tx_payload_t *frames,
    uint16_t count, esp32_mquickjs_wifi_raw_tx_payload_t *removed, uint16_t removed_capacity,
    esp32_mquickjs_wifi_raw_tx_admission_t *admission, esp32_mquickjs_wifi_raw_tx_validation_t *validation);
/* Atomic single-packet admission + exact result registration. Caller supplies
 * a zeroed, independently-owned record and empty token, neither overlapping any
 * input/output/control/payload. No allocations occur. Success consumes payload
 * and owns an independent Session ref until result_release. Caller retains this
 * record at its fixed address until successful release; access results ONLY via
 * result_status. Future teardown must unregister before freeing its storage.
 * Failure leaves packet, queue, record, token and admission unchanged.
 * Release stops observing, not transmitting: an admitted queued/in-flight packet
 * can still transmit after a public deadline. Queue/native ownership is separate. */
esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_session_admit_result(
    esp32_mquickjs_wifi_raw_tx_session_t *session, esp32_mquickjs_wifi_raw_tx_payload_t *frame,
    esp32_mquickjs_wifi_raw_tx_payload_t *removed, uint16_t removed_capacity,
    esp32_mquickjs_wifi_raw_tx_admission_t *admission, esp32_mquickjs_wifi_raw_tx_validation_t *validation,
    esp32_mquickjs_wifi_raw_tx_result_record_t *record, esp32_mquickjs_wifi_raw_tx_result_token_t *token);
bool esp32_mquickjs_wifi_raw_tx_session_result_status(esp32_mquickjs_wifi_raw_tx_session_t *session,
    const esp32_mquickjs_wifi_raw_tx_result_token_t *token, esp32_mquickjs_wifi_raw_tx_result_t *output);
bool esp32_mquickjs_wifi_raw_tx_session_result_release(esp32_mquickjs_wifi_raw_tx_session_t *session,
    esp32_mquickjs_wifi_raw_tx_result_token_t *token);
/* Periodic child reservation is serialized with Session close. It owns an
 * independent Session reference until release; close cannot publish closed while
 * any child still owns timer/worker/template controls. Native adapter only. */
bool esp32_mquickjs_wifi_raw_tx_session_periodic_acquire(esp32_mquickjs_wifi_raw_tx_session_t *session,
    const esp32_mquickjs_wifi_raw_tx_payload_t *frame);
void esp32_mquickjs_wifi_raw_tx_session_periodic_release(esp32_mquickjs_wifi_raw_tx_session_t *session);
/* Same result ownership as admit_result, but requires an idle Session queue and
 * never evicts another producer. FULL reports contention without mutation. */
esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_session_admit_periodic(
    esp32_mquickjs_wifi_raw_tx_session_t *session, esp32_mquickjs_wifi_raw_tx_payload_t *frame,
    esp32_mquickjs_wifi_raw_tx_admission_t *admission, esp32_mquickjs_wifi_raw_tx_validation_t *validation,
    esp32_mquickjs_wifi_raw_tx_result_record_t *record, esp32_mquickjs_wifi_raw_tx_result_token_t *token);
/* A successful flush_begin owns an independent Session reference until exact
 * flush_release. Public timeout releases this watcher, never the native packet.
 * Closing keeps outstanding watcher results readable and does not cancel them. */
esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_session_flush_begin(
    esp32_mquickjs_wifi_raw_tx_session_t *session, esp32_mquickjs_wifi_raw_tx_flush_token_t *token);
bool esp32_mquickjs_wifi_raw_tx_session_flush_status(esp32_mquickjs_wifi_raw_tx_session_t *session,
    const esp32_mquickjs_wifi_raw_tx_flush_token_t *token, esp32_mquickjs_wifi_raw_tx_flush_status_t *output);
bool esp32_mquickjs_wifi_raw_tx_session_flush_release(esp32_mquickjs_wifi_raw_tx_session_t *session,
    esp32_mquickjs_wifi_raw_tx_flush_token_t *token);

/* Called by the existing Raw TX poller and runtime prepare hook. Service uses
 * nonblocking Session mutex acquisition and queues at most one worker/Session.
 * No new async-poller entry, task, timer, observer queue or runtime wake pointer.
 * Native periodic scheduling will supply its own bounded trigger in W-04B. */
/* Runtime task only; periodic/background workers must use ordinary service.
 * Snapshot references and worker_busy protect Session storage across helper calls. */
bool esp32_mquickjs_wifi_raw_tx_sessions_runtime_service(void);
bool esp32_mquickjs_wifi_raw_tx_sessions_service(void);
void esp32_mquickjs_wifi_raw_tx_sessions_request_close(void);
bool esp32_mquickjs_wifi_raw_tx_sessions_drained(void);
void esp32_mquickjs_wifi_raw_tx_sessions_status(esp32_mquickjs_wifi_raw_tx_sessions_status_t *output);
#endif
