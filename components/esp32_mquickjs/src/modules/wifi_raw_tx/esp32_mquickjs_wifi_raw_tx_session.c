#include "esp32_mquickjs_wifi_raw_tx_ap.h"
#include "esp32_mquickjs_wifi_raw_tx_session.h"
#include "esp32_mquickjs_wifi_raw_tx_pump.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_raw_tx_lane.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_memory.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <string.h>

typedef esp32_mquickjs_wifi_raw_tx_session_t session_t;
typedef esp32_mquickjs_wifi_raw_tx_payload_t payload_t;
struct esp32_mquickjs_wifi_raw_tx_session {
    StaticSemaphore_t mutex_storage;
    SemaphoreHandle_t mutex;
    esp32_mquickjs_wifi_raw_tx_session_options_t options;
    esp32_mquickjs_wifi_raw_tx_queue_t queue;
    esp32_mquickjs_wifi_raw_tx_queue_slot_t *slots;
    /* Registry lock protects references; task mutex protects mutable records.
     * SDK work uses local copies while worker_busy excludes a second worker. */
    uint32_t references;
    bool cleanup_hold;
    atomic_bool close_requested, closed;
    bool open_complete, faulted, worker_busy, stop_pending, lane_acquired;
    int64_t next_service_us;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
    uint8_t channel;
    esp32_mquickjs_wifi_radio_lease_t lease;
    esp32_mquickjs_wifi_raw_tx_ap_context_t ap;
    bool ap_physical_terminated;
    esp32_mquickjs_wifi_raw_tx_lane_token_t lane;
    esp32_mquickjs_wifi_raw_tx_token_t native_token;
    esp32_mquickjs_wifi_raw_tx_ticket_t ticket;
    struct {
        esp32_mquickjs_wifi_raw_tx_token_t token;
        esp32_mquickjs_wifi_raw_tx_ticket_t ticket;
    } pending[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_IN_FLIGHT];
    uint32_t last_sequence;
    esp32_mquickjs_wifi_raw_tx_broker_status_t last_completion;
    uint8_t periodic_children;
    uint32_t next_result_identity;
    esp32_mquickjs_wifi_raw_tx_result_record_t *results[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS];
};
static portMUX_TYPE s_sessions_lock = portMUX_INITIALIZER_UNLOCKED;
static session_t *s_sessions[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS];
static uint32_t s_next_session_generation = 1;

bool esp32_mquickjs_wifi_raw_tx_session_retain(session_t *session)
{
    if (session == NULL) return false;
    portENTER_CRITICAL(&s_sessions_lock);
    bool ok = session->references != 0U && session->references != UINT32_MAX;
    if (ok) ++session->references;
    portEXIT_CRITICAL(&s_sessions_lock);
    return ok;
}

void esp32_mquickjs_wifi_raw_tx_session_release(session_t *session)
{
    if (session == NULL) return;
    portENTER_CRITICAL(&s_sessions_lock);
    bool destroy = session->references == 1U && atomic_load_explicit(&session->closed, memory_order_acquire);
    if (session->references > 1U || destroy) --session->references;
    if (destroy) for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS; ++i)
        if (s_sessions[i] == session) s_sessions[i] = NULL;
    portEXIT_CRITICAL(&s_sessions_lock);
    if (!destroy) return;
    /* Every worker, snapshot and flush watcher owns a reference. Therefore the
     * final closed reference cannot race a queue/SDK operation or live watcher. */
    bool retired = esp32_mquickjs_wifi_raw_tx_queue_deinit(&session->queue);
    configASSERT(retired);
    vSemaphoreDelete(session->mutex);
    esp32_mquickjs_memory_payload_free(session->slots);
    esp32_mquickjs_memory_payload_free(session);
}

static size_t sessions_snapshot(session_t **output)
{
    size_t count = 0;
    portENTER_CRITICAL(&s_sessions_lock);
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS; ++i) {
        session_t *session = s_sessions[i];
        if (session == NULL || session->references == 0U || session->references == UINT32_MAX) continue;
        ++session->references;
        output[count++] = session;
    }
    portEXIT_CRITICAL(&s_sessions_lock);
    return count;
}

void esp32_mquickjs_wifi_raw_tx_session_request_close(session_t *session)
{
    if (session != NULL) {
        atomic_store_explicit(&session->close_requested, true, memory_order_release);
        esp32_mquickjs_wifi_raw_tx_pump_wake();
    }
}

void esp32_mquickjs_wifi_raw_tx_sessions_request_close(void)
{
    portENTER_CRITICAL(&s_sessions_lock);
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS; ++i)
        if (s_sessions[i] != NULL) atomic_store_explicit(&s_sessions[i]->close_requested, true, memory_order_release);
    portEXIT_CRITICAL(&s_sessions_lock);
}

bool esp32_mquickjs_wifi_raw_tx_sessions_drained(void)
{
    bool drained = true;
    portENTER_CRITICAL(&s_sessions_lock);
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS; ++i)
        if (s_sessions[i] != NULL && !atomic_load_explicit(&s_sessions[i]->closed, memory_order_acquire)) drained = false;
    portEXIT_CRITICAL(&s_sessions_lock);
    return drained;
}

bool esp32_mquickjs_wifi_raw_tx_sessions_need_service(void)
{
    session_t *sessions[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS];
    size_t count = sessions_snapshot(sessions);
    bool pending = false;
    for (size_t i = 0; i < count; ++i) {
        session_t *session = sessions[i];
        if (xSemaphoreTake(session->mutex, 0) != pdTRUE) pending = true;
        else {
            if (!atomic_load_explicit(&session->closed, memory_order_acquire) &&
                (session->worker_busy || !session->open_complete || session->queue.queued ||
                 session->queue.in_flight || atomic_load_explicit(&session->close_requested, memory_order_acquire))) pending = true;
            xSemaphoreGive(session->mutex);
        }
        esp32_mquickjs_wifi_raw_tx_session_release(session);
    }
    return pending;
}

void esp32_mquickjs_wifi_raw_tx_sessions_status(esp32_mquickjs_wifi_raw_tx_sessions_status_t *output)
{
    if (output == NULL) return;
    esp32_mquickjs_wifi_raw_tx_sessions_status_t status = {0};
    session_t *sessions[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS];
    size_t count = sessions_snapshot(sessions);
    for (size_t i = 0; i < count; ++i) {
        session_t *session = sessions[i];
        xSemaphoreTake(session->mutex, portMAX_DELAY);
        bool closed = atomic_load_explicit(&session->closed, memory_order_acquire);
        if (closed) ++status.closed; else ++status.live;
        if (!closed && atomic_load_explicit(&session->close_requested, memory_order_acquire)) ++status.closing;
        if (session->faulted) ++status.faulted;
        for (unsigned j = 0; j < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS; ++j)
            if (session->results[j] != NULL) ++status.pending_results;
        for (unsigned j = 0; j < ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_FLUSHES; ++j)
            if (session->queue.flushes[j].identity != 0U) ++status.pending_flushes;
        if (status.error_generation == 0U && (session->error != ESP_OK || session->cleanup_error != ESP_OK)) {
            status.error_generation = session->queue.generation;
            status.error = session->error; status.stage = session->stage;
            status.cleanup_error = session->cleanup_error; status.cleanup_stage = session->cleanup_stage;
        }
        xSemaphoreGive(session->mutex);
        esp32_mquickjs_wifi_raw_tx_session_release(session);
    }
    *output = status;
}

esp_err_t esp32_mquickjs_wifi_raw_tx_session_new(
    const esp32_mquickjs_wifi_raw_tx_session_options_t *options, session_t **output)
{
    if (options == NULL || output == NULL || *output != NULL || options->capacity == 0U ||
        options->capacity > ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_PACKETS ||
        options->max_in_flight > ESP32_MQUICKJS_WIFI_RAW_TX_MAX_IN_FLIGHT ||
        options->max_in_flight > options->capacity ||
        (options->capacity_bytes != 0U && (options->capacity_bytes < ESP32_MQUICKJS_WIFI_RAW_TX_MIN_FRAME_BYTES ||
         options->capacity_bytes > (uint32_t)options->capacity * ESP32_MQUICKJS_WIFI_RAW_TX_MAX_FRAME_BYTES)) ||
        (options->interface != ESP32_MQUICKJS_WIFI_RAW_TX_STATION && options->interface != ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT) ||
        (options->overflow != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECT_NEWEST &&
         options->overflow != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (options->interface == ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT) return ESP_ERR_NOT_SUPPORTED;
#endif
    if (options->rate_set) {
        if (!esp32_mquickjs_wifi_tx_rate_valid(&options->rate)) return ESP_ERR_INVALID_ARG;
    }
    if (options->channel > 14U) {
#if CONFIG_SOC_WIFI_SUPPORT_5G
        if (esp32_mquickjs_wifi_radio_5ghz_channel_bit(options->channel) == 0U) return ESP_ERR_INVALID_ARG;
#else
        return ESP_ERR_INVALID_ARG;
#endif
    }
    esp_err_t pump_error = esp32_mquickjs_wifi_raw_tx_pump_init();
    if (pump_error != ESP_OK) return pump_error;
    session_t *session = esp32_mquickjs_memory_wireless_calloc("wifi.raw-tx", 1, sizeof(*session), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (session == NULL) return ESP_ERR_NO_MEM;
    session->slots = esp32_mquickjs_memory_wireless_calloc("wifi.raw-tx", options->capacity, sizeof(*session->slots), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_QUEUE);
    if (session->slots == NULL) { esp32_mquickjs_memory_payload_free(session); return ESP_ERR_NO_MEM; }
    session->mutex = xSemaphoreCreateMutexStatic(&session->mutex_storage);
    if (session->mutex == NULL) { esp32_mquickjs_memory_payload_free(session->slots); esp32_mquickjs_memory_payload_free(session); return ESP_ERR_NO_MEM; }
    session->options = *options;
    if (session->options.max_in_flight == 0U) session->options.max_in_flight = ESP32_MQUICKJS_WIFI_RAW_TX_DEFAULT_MAX_IN_FLIGHT;
    if (session->options.capacity_bytes == 0U) session->options.capacity_bytes = (uint32_t)options->capacity * ESP32_MQUICKJS_WIFI_RAW_TX_MAX_FRAME_BYTES;
    session->references = 2; /* caller + native cleanup */
    session->cleanup_hold = true;
    session->next_result_identity = 1;
    atomic_init(&session->close_requested, false);
    atomic_init(&session->closed, false);
    /* Burn identity before initialization; never publish partially initialized
     * control to the registry, and never recycle an identity after any failure. */
    portENTER_CRITICAL(&s_sessions_lock);
    uint32_t generation = s_next_session_generation;
    if (generation != 0U) s_next_session_generation = generation == UINT32_MAX ? 0U : generation + 1U;
    portEXIT_CRITICAL(&s_sessions_lock);
    bool initialized = generation != 0U && esp32_mquickjs_wifi_raw_tx_queue_init(
        &session->queue, session->slots, options->capacity, generation);
    if (initialized) initialized = esp32_mquickjs_wifi_raw_tx_queue_limits(&session->queue,
        session->options.capacity_bytes, session->options.max_in_flight);
    bool published = false;
    if (initialized) {
        portENTER_CRITICAL(&s_sessions_lock);
        for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS; ++i) if (s_sessions[i] == NULL) {
            s_sessions[i] = session; published = true; break;
        }
        portEXIT_CRITICAL(&s_sessions_lock);
    }
    if (!published) {
        vSemaphoreDelete(session->mutex);
        esp32_mquickjs_memory_payload_free(session->slots); esp32_mquickjs_memory_payload_free(session);
        return ESP_ERR_INVALID_STATE;
    }
    *output = session;
    esp32_mquickjs_wifi_raw_tx_pump_wake();
    return ESP_OK;
}

bool esp32_mquickjs_wifi_raw_tx_session_status(session_t *session,
    esp32_mquickjs_wifi_raw_tx_session_status_t *output)
{
    if (session == NULL || output == NULL) return false;
    if (xSemaphoreTake(session->mutex, portMAX_DELAY) != pdTRUE) return false;
    *output = (esp32_mquickjs_wifi_raw_tx_session_status_t){
        .generation = session->queue.generation, .radio_generation = session->lease.generation,
        .active_sequence = session->queue.active != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_NONE
            ? session->slots[session->queue.active].sequence : 0U,
        .lane_identity = session->lane.identity,
        .channel = session->channel, .queued = session->queue.queued, .capacity = session->queue.capacity,
        .in_flight = session->queue.in_flight, .max_in_flight = session->queue.max_in_flight,
        .capacity_bytes = session->queue.capacity_bytes, .used_bytes = session->queue.used_bytes,
        .high_water_bytes = session->queue.high_water_bytes,
        .remaining_sequences = UINT32_MAX - session->queue.last_sequence,
        .periodic_children = session->periodic_children,
        .open_complete = session->open_complete, .faulted = session->faulted, .worker_busy = session->worker_busy,
        .close_requested = atomic_load_explicit(&session->close_requested, memory_order_acquire),
        .closed = atomic_load_explicit(&session->closed, memory_order_acquire),
        .error = session->error, .stage = session->stage, .cleanup_error = session->cleanup_error,
        .cleanup_stage = session->cleanup_stage, .totals = session->queue.totals,
        .last_sequence = session->last_sequence, .last_completion = session->last_completion,
    };
    xSemaphoreGive(session->mutex);
    return true;
}

/* These result helpers run only under the Session mutex. A record pointer is
 * never retained outside that mutex, so unregister may immediately return its
 * storage to a finished Future even while the SDK still owns the packet. */
static void session_result_publish(session_t *session, uint32_t sequence,
    esp32_mquickjs_wifi_raw_tx_result_kind_t kind, esp_err_t error, const char *stage,
    uint8_t channel, const esp32_mquickjs_wifi_raw_tx_broker_status_t *native)
{
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS; ++i) {
        esp32_mquickjs_wifi_raw_tx_result_record_t *record = session->results[i];
        if (record == NULL || record->result.sequence != sequence ||
            (record->result.kind != ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_PENDING &&
             !(record->result.kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_UNCERTAIN &&
               kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_TERMINATED && native != NULL && native->native_terminated))) continue;
        record->result.kind = kind; record->result.error = error; record->result.stage = stage;
        record->result.channel = channel;
        if (native != NULL) record->result.native = *native;
        record->payload = NULL;
    }
}

static void session_results_drop(session_t *session, const payload_t *removed, uint16_t count, const char *stage)
{
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS; ++i) {
        esp32_mquickjs_wifi_raw_tx_result_record_t *record = session->results[i];
        if (record == NULL || record->result.kind != ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_PENDING) continue;
        for (unsigned j = 0; j < count; ++j) if (record->payload == removed[j].data) {
            session_result_publish(session, record->result.sequence, ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_DROPPED,
                ESP_ERR_INVALID_STATE, stage, session->channel, NULL);
            break;
        }
    }
}

static bool session_validate_frames(session_t *session, const payload_t *frames, uint16_t count,
    esp32_mquickjs_wifi_raw_tx_validation_t *validation)
{
    if (session == NULL || frames == NULL || count == 0U || count > session->options.capacity || validation == NULL)
        return false;
    esp32_mquickjs_wifi_raw_tx_validation_policy_t policy = {
        .interface = session->options.interface, .driver_sequence = session->options.driver_sequence};
    for (unsigned i = 0; i < count; ++i) {
        esp32_mquickjs_wifi_raw_tx_validated_frame_t frame;
        *validation = esp32_mquickjs_wifi_raw_tx_validate(frames[i].data, frames[i].length, &policy, &frame);
        if (*validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID) return false;
    }
    return true;
}

static esp32_mquickjs_wifi_raw_tx_queue_result_t session_admit_locked(session_t *session,
    payload_t *frames, uint16_t count, payload_t *removed, uint16_t removed_capacity,
    esp32_mquickjs_wifi_raw_tx_admission_t *admission)
{
    esp32_mquickjs_wifi_raw_tx_queue_result_t result = ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_CLOSED;
    if (session->open_complete && !session->faulted &&
        !atomic_load_explicit(&session->close_requested, memory_order_acquire)) {
        result = esp32_mquickjs_wifi_raw_tx_queue_admit(&session->queue, frames, count,
            session->options.overflow, removed, removed_capacity, admission);
        if (result == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) {
            session_results_drop(session, removed, admission->evicted_packets, "queue-overflow");
            session->next_service_us = 0;
        }
    }
    return result;
}

esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_session_admit(
    session_t *session, payload_t *frames, uint16_t count, payload_t *removed, uint16_t removed_capacity,
    esp32_mquickjs_wifi_raw_tx_admission_t *admission, esp32_mquickjs_wifi_raw_tx_validation_t *validation)
{
    if (!session_validate_frames(session, frames, count, validation) ||
        xSemaphoreTake(session->mutex, portMAX_DELAY) != pdTRUE) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID;
    esp32_mquickjs_wifi_raw_tx_queue_result_t result = session_admit_locked(session, frames, count, removed, removed_capacity, admission);
    xSemaphoreGive(session->mutex);
    if (result == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) esp32_mquickjs_wifi_raw_tx_pump_wake();
    return result;
}

static esp32_mquickjs_wifi_raw_tx_queue_result_t session_admit_result(
    session_t *session, payload_t *frame, payload_t *removed, uint16_t removed_capacity,
    esp32_mquickjs_wifi_raw_tx_admission_t *admission, esp32_mquickjs_wifi_raw_tx_validation_t *validation,
    esp32_mquickjs_wifi_raw_tx_result_record_t *record, esp32_mquickjs_wifi_raw_tx_result_token_t *token, bool idle_only)
{
    if (record == NULL || token == NULL || record->registered || record->identity != 0U ||
        token->generation != 0U || token->identity != 0U || token->index != 0U ||
        !session_validate_frames(session, frame, 1, validation)) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID;
    if (!esp32_mquickjs_wifi_raw_tx_session_retain(session)) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_EXHAUSTED;
    esp32_mquickjs_wifi_raw_tx_queue_result_t result = ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID;
    if (xSemaphoreTake(session->mutex, portMAX_DELAY) == pdTRUE) {
        unsigned index = 0;
        while (index < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS && session->results[index] != NULL) ++index;
        if (session->next_result_identity == 0U) result = ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_EXHAUSTED;
        else if (index == ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS ||
            (idle_only && (session->queue.queued != 0U ||
                session->queue.active != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_NONE || session->worker_busy)))
            result = ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FULL;
        else {
            const uint8_t *payload = frame->data;
            result = session_admit_locked(session, frame, 1, removed, removed_capacity, admission);
            if (result == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) {
                uint32_t identity = session->next_result_identity;
                session->next_result_identity = identity == UINT32_MAX ? 0U : identity + 1U;
                *record = (esp32_mquickjs_wifi_raw_tx_result_record_t){.registered = true, .identity = identity,
                    .payload = payload, .result = {.generation = session->queue.generation,
                        .sequence = admission->first_sequence, .kind = ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_PENDING}};
                session->results[index] = record;
                *token = (esp32_mquickjs_wifi_raw_tx_result_token_t){session->queue.generation, identity, (uint8_t)index};
            }
        }
        xSemaphoreGive(session->mutex);
    }
    if (result == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) esp32_mquickjs_wifi_raw_tx_pump_wake();
    if (result != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) esp32_mquickjs_wifi_raw_tx_session_release(session);
    return result;
}

esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_session_admit_result(
    session_t *session, payload_t *frame, payload_t *removed, uint16_t removed_capacity,
    esp32_mquickjs_wifi_raw_tx_admission_t *admission, esp32_mquickjs_wifi_raw_tx_validation_t *validation,
    esp32_mquickjs_wifi_raw_tx_result_record_t *record, esp32_mquickjs_wifi_raw_tx_result_token_t *token)
{
    return session_admit_result(session, frame, removed, removed_capacity, admission, validation, record, token, false);
}

esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_session_admit_periodic(
    session_t *session, payload_t *frame, esp32_mquickjs_wifi_raw_tx_admission_t *admission,
    esp32_mquickjs_wifi_raw_tx_validation_t *validation, esp32_mquickjs_wifi_raw_tx_result_record_t *record,
    esp32_mquickjs_wifi_raw_tx_result_token_t *token)
{
    payload_t removed[ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_PACKETS] = {0};
    return session_admit_result(session, frame, removed, ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_PACKETS,
        admission, validation, record, token, true);
}

bool esp32_mquickjs_wifi_raw_tx_session_periodic_acquire(session_t *session, const payload_t *frame)
{
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    if (!session_validate_frames(session, frame, 1, &validation)) return false;
    if (!esp32_mquickjs_wifi_raw_tx_session_retain(session)) return false;
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    bool ok = session->open_complete && !session->faulted && session->periodic_children < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS &&
        !atomic_load_explicit(&session->close_requested, memory_order_acquire);
    if (ok) ++session->periodic_children;
    xSemaphoreGive(session->mutex);
    if (!ok) esp32_mquickjs_wifi_raw_tx_session_release(session);
    return ok;
}

void esp32_mquickjs_wifi_raw_tx_session_periodic_release(session_t *session)
{
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    configASSERT(session->periodic_children != 0U);
    --session->periodic_children;
    xSemaphoreGive(session->mutex);
    esp32_mquickjs_wifi_raw_tx_session_release(session);
}

static esp32_mquickjs_wifi_raw_tx_result_record_t *session_result_exact(session_t *session,
    const esp32_mquickjs_wifi_raw_tx_result_token_t *token)
{
    if (token == NULL || token->generation != session->queue.generation || token->identity == 0U ||
        token->index >= ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS) return NULL;
    esp32_mquickjs_wifi_raw_tx_result_record_t *record = session->results[token->index];
    return record != NULL && record->registered && record->identity == token->identity ? record : NULL;
}

bool esp32_mquickjs_wifi_raw_tx_session_result_status(session_t *session,
    const esp32_mquickjs_wifi_raw_tx_result_token_t *token, esp32_mquickjs_wifi_raw_tx_result_t *output)
{
    if (session == NULL || output == NULL || xSemaphoreTake(session->mutex, portMAX_DELAY) != pdTRUE) return false;
    esp32_mquickjs_wifi_raw_tx_result_record_t *record = session_result_exact(session, token);
    if (record != NULL) *output = record->result;
    xSemaphoreGive(session->mutex);
    return record != NULL;
}

bool esp32_mquickjs_wifi_raw_tx_session_result_release(session_t *session,
    esp32_mquickjs_wifi_raw_tx_result_token_t *token)
{
    if (session == NULL || xSemaphoreTake(session->mutex, portMAX_DELAY) != pdTRUE) return false;
    esp32_mquickjs_wifi_raw_tx_result_record_t *record = session_result_exact(session, token);
    if (record != NULL) {
        session->results[token->index] = NULL;
        memset(record, 0, sizeof(*record));
        *token = (esp32_mquickjs_wifi_raw_tx_result_token_t){0};
    }
    xSemaphoreGive(session->mutex);
    if (record != NULL) esp32_mquickjs_wifi_raw_tx_session_release(session);
    return record != NULL;
}

esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_session_flush_begin(
    session_t *session, esp32_mquickjs_wifi_raw_tx_flush_token_t *token)
{
    if (!esp32_mquickjs_wifi_raw_tx_session_retain(session)) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_EXHAUSTED;
    if (xSemaphoreTake(session->mutex, portMAX_DELAY) != pdTRUE) {
        esp32_mquickjs_wifi_raw_tx_session_release(session); return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID;
    }
    esp32_mquickjs_wifi_raw_tx_queue_result_t result = esp32_mquickjs_wifi_raw_tx_queue_flush_begin(&session->queue, token);
    xSemaphoreGive(session->mutex);
    if (result == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) esp32_mquickjs_wifi_raw_tx_pump_wake();
    if (result != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) esp32_mquickjs_wifi_raw_tx_session_release(session);
    return result;
}

bool esp32_mquickjs_wifi_raw_tx_session_flush_status(session_t *session,
    const esp32_mquickjs_wifi_raw_tx_flush_token_t *token, esp32_mquickjs_wifi_raw_tx_flush_status_t *output)
{
    if (session == NULL || xSemaphoreTake(session->mutex, portMAX_DELAY) != pdTRUE) return false;
    bool ok = esp32_mquickjs_wifi_raw_tx_queue_flush_status(&session->queue, token, output);
    xSemaphoreGive(session->mutex);
    return ok;
}

bool esp32_mquickjs_wifi_raw_tx_session_flush_release(session_t *session,
    esp32_mquickjs_wifi_raw_tx_flush_token_t *token)
{
    if (session == NULL || xSemaphoreTake(session->mutex, portMAX_DELAY) != pdTRUE) return false;
    bool ok = esp32_mquickjs_wifi_raw_tx_queue_flush_release(&session->queue, token);
    xSemaphoreGive(session->mutex);
    if (ok) esp32_mquickjs_wifi_raw_tx_session_release(session);
    return ok;
}

/* Task mutex held, no SDK work or freeing. First operational error survives
 * later cleanup retries; cleanup errors have their own diagnostic suffix. */
static void session_fault(session_t *session, esp_err_t error, const char *stage)
{
    if (!session->faulted) { session->error = error; session->stage = stage; }
    session->faulted = true;
    atomic_store_explicit(&session->close_requested, true, memory_order_release);
}

static void session_drop_queued(session_t *session)
{
    payload_t removed[ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_PACKETS] = {0};
    uint16_t count = 0;
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    bool ok = esp32_mquickjs_wifi_raw_tx_queue_close(&session->queue, removed, session->options.capacity, &count);
    configASSERT(ok);
    session_results_drop(session, removed, count, "session-close");
    xSemaphoreGive(session->mutex);
    for (unsigned i = 0; i < count; ++i) esp32_mquickjs_memory_payload_free(removed[i].data);
}

static void session_packet_step(session_t *session,
    const esp32_mquickjs_wifi_radio_lease_t *owner,
    esp32_mquickjs_wifi_raw_tx_token_t *native_token,
    esp32_mquickjs_wifi_raw_tx_ticket_t *queue_ticket, uint8_t *actual_channel,
    bool *terminated)
{
    esp32_mquickjs_wifi_radio_lease_t lease = *owner;
    esp32_mquickjs_wifi_raw_tx_token_t token = *native_token;
    esp32_mquickjs_wifi_raw_tx_ticket_t ticket = *queue_ticket;
    uint8_t channel = *actual_channel;
    bool ap_physical_terminated = *terminated;
    bool close_requested = atomic_load_explicit(&session->close_requested, memory_order_acquire);
    esp_err_t error = ESP_OK;
    const char *stage = NULL;
    if (token.identity == 0U && !close_requested) {
        payload_t borrowed = {0};
        xSemaphoreTake(session->mutex, portMAX_DELAY);
        bool take = !session->faulted && !atomic_load_explicit(&session->close_requested, memory_order_acquire) &&
            esp32_mquickjs_wifi_raw_tx_queue_take(&session->queue, &ticket, &borrowed);
        xSemaphoreGive(session->mutex);
        if (take) {
            esp32_mquickjs_wifi_raw_tx_validation_t validation;
            if (atomic_load_explicit(&session->close_requested, memory_order_acquire)) {
                /* No SDK submit happened: this active payload is safe to abort. */
                payload_t retired = {0};
                xSemaphoreTake(session->mutex, portMAX_DELAY);
                uint32_t sequence = ticket.sequence;
                bool ok = esp32_mquickjs_wifi_raw_tx_queue_finish(&session->queue, &ticket,
                    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_ABORTED, &retired);
                configASSERT(ok);
                if (ok) session_result_publish(session, sequence, ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_ABORTED,
                    ESP_ERR_INVALID_STATE, "close-before-submit", channel, NULL);
                xSemaphoreGive(session->mutex);
                esp32_mquickjs_memory_payload_free(retired.data);
            } else {
                error = esp32_mquickjs_wifi_radio_raw_tx_submit(&lease, session->options.interface,
                    session->options.driver_sequence, borrowed.data, borrowed.length, &token, &validation, &channel);
                xSemaphoreTake(session->mutex, portMAX_DELAY);
                if (error == ESP_OK) {
                    bool ok = token.identity != 0U && esp32_mquickjs_wifi_raw_tx_queue_accept(&session->queue, &ticket);
                    configASSERT(ok);
                    if (ok) for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_RESULTS; ++i) {
                        esp32_mquickjs_wifi_raw_tx_result_record_t *record = session->results[i];
                        if (record != NULL && record->result.sequence == ticket.sequence)
                            record->result.driver_submitted = true;
                    }
                }
                xSemaphoreGive(session->mutex);
                if (error != ESP_OK) {
                    if (token.identity == 0U) {
                        payload_t retired = {0};
                        xSemaphoreTake(session->mutex, portMAX_DELAY);
                        uint32_t sequence = ticket.sequence;
                        bool ok = esp32_mquickjs_wifi_raw_tx_queue_finish(&session->queue, &ticket,
                            ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECTED, &retired);
                        configASSERT(ok);
                        session_fault(session, error, "submit");
                        if (ok) session_result_publish(session, sequence, ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_REJECTED,
                            error, "submit", channel, NULL);
                        xSemaphoreGive(session->mutex);
                        esp32_mquickjs_memory_payload_free(retired.data);
                    }
                    stage = "submit"; goto fault;
                }
            }
        }
    }
    if (token.identity != 0U) {
        esp32_mquickjs_wifi_raw_tx_broker_status_t native = {0};
        (void)esp32_mquickjs_wifi_raw_tx_broker_result(&token, &native);
        if (native.token.identity != token.identity || native.token.generation != token.generation ||
            native.token.radio_lease_identity != token.radio_lease_identity ||
            (native.correlation_fault && !native.native_terminated)) {
            error = ESP_ERR_INVALID_STATE; stage = "completion-identity-correlation"; goto fault;
        }
        /* Retire may race a callback after the snapshot. Only attempt it when
         * this exact snapshot already contains completion; otherwise a newly
         * completed operation could be retired with an older unknown result. */
        if ((!native.driver_completed && !native.native_terminated) || native.callbacks_active != 0U) goto packet_done;
        if (!esp32_mquickjs_wifi_radio_raw_tx_retire(&lease, &token)) goto packet_done;
        payload_t retired = {0};
        esp32_mquickjs_wifi_raw_tx_queue_outcome_t outcome = native.native_terminated
            ? ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_ABORTED : native.completion.status == ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS
            ? ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS : native.completion.status == ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED
            ? ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FAILED : ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_UNKNOWN;
        xSemaphoreTake(session->mutex, portMAX_DELAY);
        session->last_sequence = ticket.sequence;
        session->last_completion = native;
        bool ok = esp32_mquickjs_wifi_raw_tx_queue_finish(&session->queue, &ticket, outcome, &retired);
        configASSERT(ok);
        if (native.native_terminated) {
            ap_physical_terminated = true;
            esp_err_t terminated_error = native.submit_error != ESP_OK ? native.submit_error : ESP_ERR_INVALID_STATE;
            session_fault(session, terminated_error, "native-terminated");
            if (ok) session_result_publish(session, session->last_sequence, ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_TERMINATED,
                terminated_error, "native-terminated", channel, &native);
        } else if (ok) session_result_publish(session, session->last_sequence, ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_COMPLETED,
            ESP_OK, NULL, channel, &native);
        xSemaphoreGive(session->mutex);
        esp32_mquickjs_memory_payload_free(retired.data);
    }
    goto packet_done;
fault:
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    session_fault(session, error, stage);
    xSemaphoreGive(session->mutex);
    if (ticket.sequence != 0U && token.identity != 0U) {
        esp32_mquickjs_wifi_raw_tx_broker_status_t native = {0};
        (void)esp32_mquickjs_wifi_raw_tx_broker_result(&token, &native);
        if (native.token.identity != token.identity || native.token.generation != token.generation ||
            native.token.radio_lease_identity != token.radio_lease_identity) {
            native = (esp32_mquickjs_wifi_raw_tx_broker_status_t){.token = token};
        }
        xSemaphoreTake(session->mutex, portMAX_DELAY);
        session_result_publish(session, ticket.sequence, ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_UNCERTAIN,
            error, stage, channel, &native);
        xSemaphoreGive(session->mutex);
    }
packet_done:
    *native_token = token;
    *queue_ticket = ticket;
    *actual_channel = channel;
    *terminated = ap_physical_terminated;
}

static void session_worker(void *opaque)
{
    session_t *session = opaque;
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    esp32_mquickjs_wifi_radio_lease_t lease = session->lease;
    esp32_mquickjs_wifi_raw_tx_lane_token_t lane = session->lane;
    esp32_mquickjs_wifi_raw_tx_token_t token = session->native_token;
    esp32_mquickjs_wifi_raw_tx_ticket_t ticket = session->ticket;
    bool lane_acquired = session->lane_acquired, stop_pending = session->stop_pending;
    bool ap_physical_terminated = session->ap_physical_terminated;
    bool opening = !session->open_complete, work = opening || session->queue.queued != 0U;
    uint8_t channel = session->channel;
    xSemaphoreGive(session->mutex);
    esp_err_t error = ESP_OK, cleanup_error = ESP_OK;
    const char *stage = NULL, *cleanup_stage = NULL;
    bool close_requested = atomic_load_explicit(&session->close_requested, memory_order_acquire);
    if (close_requested) session_drop_queued(session);
    if (work && !close_requested && lane.identity == 0U) {
        esp32_mquickjs_wifi_raw_tx_lane_result_t result = esp32_mquickjs_wifi_raw_tx_lane_request(&lane);
        if (result == ESP32_MQUICKJS_WIFI_RAW_TX_LANE_FULL) goto publish;
        if (result != ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK) {
            error = ESP_ERR_INVALID_STATE; stage = "lane-request"; goto fault;
        }
    }
    if (lane.identity != 0U && !lane_acquired && !close_requested)
        lane_acquired = esp32_mquickjs_wifi_raw_tx_lane_acquire(&lane);
    if (opening && !close_requested && lane_acquired) {
        /* Only the runtime service may prepare/retire AP helper globals. */
        if (session->options.rate_set && session->options.interface == ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT)
            goto publish;
        error = esp32_mquickjs_wifi_radio_raw_tx_acquire(session->options.interface,
            session->options.channel, session->options.rate_set ? &session->options.rate : NULL, &lease, &channel);
        if (error != ESP_OK) { stage = "radio-open"; goto fault; }
        opening = false;
    }
    close_requested = atomic_load_explicit(&session->close_requested, memory_order_acquire);
    if (!opening && lane_acquired) {
        for (unsigned i = 0; i < session->options.max_in_flight; ++i)
            session_packet_step(session, &lease, &session->pending[i].token,
                &session->pending[i].ticket, &channel, &ap_physical_terminated);
        token = (esp32_mquickjs_wifi_raw_tx_token_t){0};
        ticket = (esp32_mquickjs_wifi_raw_tx_ticket_t){0};
        for (unsigned i = 0; i < session->options.max_in_flight; ++i)
            if (session->pending[i].token.identity != 0U) {
                token = session->pending[i].token; ticket = session->pending[i].ticket; break;
            }
    }
    goto cleanup;

fault:
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    session_fault(session, error, stage);
    xSemaphoreGive(session->mutex);
    if (ticket.sequence != 0U && token.identity != 0U) {
        esp32_mquickjs_wifi_raw_tx_broker_status_t native = {0};
        esp32_mquickjs_wifi_raw_tx_broker_status(&native);
        if (native.token.identity != token.identity || native.token.generation != token.generation ||
            native.token.radio_lease_identity != token.radio_lease_identity) {
            native = (esp32_mquickjs_wifi_raw_tx_broker_status_t){.token = token};
        }
        xSemaphoreTake(session->mutex, portMAX_DELAY);
        session_result_publish(session, ticket.sequence, ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_UNCERTAIN,
            error, stage, channel, &native);
        xSemaphoreGive(session->mutex);
    }
cleanup:
    close_requested = atomic_load_explicit(&session->close_requested, memory_order_acquire);
    if (close_requested) {
        session_drop_queued(session);
        if (token.identity != 0U) {
            for (unsigned i = 0; i < session->options.max_in_flight; ++i)
                if (session->pending[i].token.identity != 0U)
                    (void)esp32_mquickjs_wifi_raw_tx_broker_abandon(&session->pending[i].token);
            cleanup_error = ESP_ERR_INVALID_STATE; cleanup_stage = "native-completion";
        } else if (session->ap.owned && !ap_physical_terminated) {
            stop_pending = true; /* Runtime exchanges rate owner for helper cleanup token. */
            cleanup_error = session->cleanup_error;
            cleanup_stage = session->cleanup_stage;
        } else if (lease.acquired || stop_pending) {
            cleanup_error = esp32_mquickjs_wifi_radio_release_and_stop_idle(&lease);
            if (lease.acquired && cleanup_error == ESP_OK) cleanup_error = ESP_ERR_INVALID_STATE;
            stop_pending = cleanup_error != ESP_OK;
            if (stop_pending) cleanup_stage = "radio-release-stop";
        }
    }
    /* A rate Session owns the interface exclusively until stopped restoration.
     * Keep its arbiter grant even while idle; failed close must not reuse it. */
    if (token.identity == 0U && !stop_pending && !session->ap.owned && lane.identity != 0U &&
        (!session->options.rate_set || close_requested)) {
        bool released = lane_acquired ? esp32_mquickjs_wifi_raw_tx_lane_release(&lane) :
            close_requested ? esp32_mquickjs_wifi_raw_tx_lane_withdraw(&lane) : false;
        if (released) lane_acquired = false;
        else if (lane_acquired || close_requested) {
            cleanup_error = ESP_ERR_INVALID_STATE; cleanup_stage = "lane-release";
        }
    }
publish:
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    session->lease = lease; session->lane = lane; session->native_token = token; session->ticket = ticket;
    session->channel = channel; session->lane_acquired = lane_acquired; session->stop_pending = stop_pending;
    session->open_complete = !opening;
    session->ap_physical_terminated = ap_physical_terminated;
    session->cleanup_error = cleanup_error; session->cleanup_stage = cleanup_stage;
    bool drop_hold = false;
    if (atomic_load_explicit(&session->close_requested, memory_order_acquire) && session->queue.closed &&
        token.identity == 0U && ticket.sequence == 0U && !lease.acquired && lane.identity == 0U &&
        !stop_pending && !session->ap.owned && cleanup_error == ESP_OK && session->periodic_children == 0U) {
        atomic_store_explicit(&session->closed, true, memory_order_release);
        drop_hold = session->cleanup_hold; session->cleanup_hold = false;
    }
    session->next_service_us = esp_timer_get_time() + (cleanup_error != ESP_OK ? ESP32_MQUICKJS_WIFI_RAW_TX_CLEANUP_RETRY_US : 0);
    session->worker_busy = false;
    xSemaphoreGive(session->mutex);
    esp32_mquickjs_wifi_raw_tx_pump_wake();
    if (drop_hold) esp32_mquickjs_wifi_raw_tx_session_release(session);
    esp32_mquickjs_wifi_raw_tx_session_release(session); /* worker reference, final access */
}

/* This entry is called only by the runtime poller/teardown. Ordinary service is
 * also called from a periodic background worker and must never touch AP globals. */
bool esp32_mquickjs_wifi_raw_tx_sessions_runtime_service(void)
{
    session_t *sessions[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS];
    size_t count = sessions_snapshot(sessions);
    bool handled = false;
    int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < count; ++i) {
        session_t *session = sessions[i];
        bool run = false, closing = false;
        esp32_mquickjs_wifi_raw_tx_ap_context_t ap = {0};
        esp32_mquickjs_wifi_radio_lease_t lease = {0};
        bool physically_terminated = false;
        uint8_t channel = 0;
        if (xSemaphoreTake(session->mutex, 0) == pdTRUE) {
            closing = atomic_load_explicit(&session->close_requested, memory_order_acquire);
            bool opening = !closing && !session->open_complete && session->lane_acquired;
            bool cleanup = closing && session->ap.owned && session->native_token.identity == 0U &&
                session->ticket.sequence == 0U && (!session->ap_physical_terminated || !session->lease.acquired);
            run = session->options.rate_set && session->options.interface == ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT &&
                !session->worker_busy && !atomic_load_explicit(&session->closed, memory_order_acquire) &&
                now >= session->next_service_us && (opening || cleanup);
            if (run) {
                session->worker_busy = true;
                ap = session->ap; lease = session->lease; channel = session->channel;
                physically_terminated = session->ap_physical_terminated;
            }
            xSemaphoreGive(session->mutex);
        }
        if (run) {
            const char *stage = NULL;
            esp_err_t err = closing
                ? esp32_mquickjs_wifi_ap_close_raw_tx_rate(&ap, &lease, physically_terminated, &stage)
                : esp32_mquickjs_wifi_ap_open_raw_tx_rate(&ap, &session->options.rate,
                    session->options.channel, &lease, &channel, &stage);
            xSemaphoreTake(session->mutex, portMAX_DELAY);
            session->ap = ap; session->lease = lease; session->channel = channel;
            if (closing) {
                session->cleanup_error = err; session->cleanup_stage = stage;
                session->stop_pending = err != ESP_OK || ap.owned || lease.acquired;
            } else if (err == ESP_OK) session->open_complete = true;
            else session_fault(session, err, stage);
            session->next_service_us = esp_timer_get_time() + (err == ESP_OK ? 0 : ESP32_MQUICKJS_WIFI_RAW_TX_CLEANUP_RETRY_US);
            session->worker_busy = false;
            xSemaphoreGive(session->mutex);
            handled = true;
        }
        esp32_mquickjs_wifi_raw_tx_session_release(session); /* snapshot reference */
    }
    return handled;
}

bool esp32_mquickjs_wifi_raw_tx_sessions_service(void)
{
    session_t *sessions[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS];
    size_t count = sessions_snapshot(sessions);
    int64_t now = esp_timer_get_time();
    bool handled = false;
    for (size_t i = 0; i < count; ++i) {
        session_t *session = sessions[i];
        bool accepted = false, worker_ref = false;
        if (xSemaphoreTake(session->mutex, 0) == pdTRUE) {
            bool closing = atomic_load_explicit(&session->close_requested, memory_order_acquire);
            bool work = !session->open_complete || session->queue.queued != 0U || session->ticket.sequence != 0U ||
                (session->lane.identity != 0U && (!session->options.rate_set || !session->lane_acquired)) || closing;
            bool submit = work && !session->worker_busy && now >= session->next_service_us &&
                !atomic_load_explicit(&session->closed, memory_order_acquire);
            if (submit && !closing && session->lane.identity != 0U && !session->lane_acquired) {
                session->lane_acquired = esp32_mquickjs_wifi_raw_tx_lane_acquire(&session->lane);
                submit = session->lane_acquired;
            }
            if (submit && !closing && session->queue.in_flight != 0U) {
                bool ready = session->queue.queued != 0U &&
                    session->queue.in_flight < session->queue.max_in_flight && !session->faulted;
                for (unsigned n = 0; n < session->options.max_in_flight; ++n) {
                    esp32_mquickjs_wifi_raw_tx_broker_status_t native = {0};
                    if (session->pending[n].token.identity != 0U &&
                        esp32_mquickjs_wifi_raw_tx_broker_result(&session->pending[n].token, &native) &&
                        (native.driver_completed || native.correlation_fault || native.native_terminated)) ready = true;
                }
                if (!ready) submit = false;
            }
            if (submit && !closing && !session->open_complete && session->lane_acquired &&
                session->options.rate_set && session->options.interface == ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT)
                submit = false; /* Runtime service owns AP startup. */
            if (submit && closing && session->ap.owned && session->queue.closed &&
                session->native_token.identity == 0U && session->ticket.sequence == 0U &&
                (!session->ap_physical_terminated || !session->lease.acquired))
                submit = false; /* Do not starve runtime helper retirement with empty workers. */
            if (submit) {
                worker_ref = esp32_mquickjs_wifi_raw_tx_session_retain(session);
                session->worker_busy = worker_ref;
                /* Queue admission is nonblocking; a started worker waits for
                 * this task mutex. On rejection restore the scheduling flag
                 * before unlocking, without another blocking mutex acquire. */
                if (worker_ref) accepted = esp32_mquickjs_submit_background_worker(session_worker, session);
                if (!accepted) { session->worker_busy = false; session->next_service_us = now + ESP32_MQUICKJS_WIFI_RAW_TX_SERVICE_RETRY_US; }
            }
            xSemaphoreGive(session->mutex);
        }
        if (accepted) handled = true;
        else if (worker_ref) esp32_mquickjs_wifi_raw_tx_session_release(session);
        /* Keep our snapshot ref through xSemaphoreGive's return even if the
         * newly unblocked worker completes close and drops all its references. */
        esp32_mquickjs_wifi_raw_tx_session_release(session);
    }
    return handled;
}
#endif
