#include "esp32_mquickjs_wifi_raw_tx_periodic_job.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_raw_tx_lane.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_memory.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <string.h>

#define ledger_api(name) esp32_mquickjs_wifi_raw_tx_periodic_##name
#define session_api(name) esp32_mquickjs_wifi_raw_tx_session_##name
#define PERIODIC(name) ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_##name
typedef esp32_mquickjs_wifi_raw_tx_periodic_job_t job_t;
typedef esp32_mquickjs_wifi_raw_tx_session_t session_t;
typedef esp32_mquickjs_wifi_raw_tx_payload_t payload_t;
struct esp32_mquickjs_wifi_raw_tx_periodic_job {
    StaticSemaphore_t mutex_storage;
    SemaphoreHandle_t mutex;
    uint32_t references; /* registry lock */
    atomic_bool worker_busy, retired;
    bool stop_requested, close_requested; /* task mutex: serializes admission */
    session_t *session;
    payload_t frame;
    esp32_mquickjs_wifi_raw_tx_periodic_t ledger;
    esp32_mquickjs_wifi_raw_tx_periodic_ticket_t ticket;
    esp32_mquickjs_wifi_raw_tx_result_record_t result;
    esp32_mquickjs_wifi_raw_tx_result_token_t result_token;
    esp_timer_handle_t timer;
    bool ready, timer_stopped, timer_transition, result_settled;
    int64_t next_service_us;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
};
static portMUX_TYPE s_jobs_lock = portMUX_INITIALIZER_UNLOCKED;
static job_t *s_jobs[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS];
static uint32_t s_next_job_generation = 1;
static void job_worker(void *opaque);

bool esp32_mquickjs_wifi_raw_tx_periodic_job_retain(job_t *job)
{
    if (job == NULL) return false;
    portENTER_CRITICAL(&s_jobs_lock);
    bool ok = job->references != 0U && job->references != UINT32_MAX;
    if (ok) ++job->references;
    portEXIT_CRITICAL(&s_jobs_lock);
    return ok;
}

void esp32_mquickjs_wifi_raw_tx_periodic_job_release(job_t *job)
{
    if (job == NULL) return;
    portENTER_CRITICAL(&s_jobs_lock);
    bool destroy = job->references == 1U && atomic_load_explicit(&job->retired, memory_order_acquire);
    if (job->references > 1U || destroy) --job->references;
    if (destroy) for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS; ++i)
        if (s_jobs[i] == job) s_jobs[i] = NULL;
    portEXIT_CRITICAL(&s_jobs_lock);
    if (!destroy) return;
    configASSERT(job->session == NULL && job->timer == NULL && job->frame.data == NULL && !job->result_token.identity);
    vSemaphoreDelete(job->mutex);
    esp32_mquickjs_memory_payload_free(job);
}

/* Called with a caller/snapshot/timer hold. At most one queued/running worker;
 * no allocation, JS, SDK call, blocking lock or runtime wake from timer context.
 * The persistent timer hold prevents a rejected enqueue's release from freeing. */
static bool job_schedule(job_t *job)
{
    if (atomic_load_explicit(&job->retired, memory_order_acquire)) return false;
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&job->worker_busy, &expected, true,
        memory_order_acq_rel, memory_order_acquire)) return false;
    if (atomic_load_explicit(&job->retired, memory_order_acquire)) {
        atomic_store_explicit(&job->worker_busy, false, memory_order_release); return false;
    }
    if (!esp32_mquickjs_wifi_raw_tx_periodic_job_retain(job)) {
        atomic_store_explicit(&job->worker_busy, false, memory_order_release); return false;
    }
    if (esp32_mquickjs_submit_background_worker(job_worker, job)) return true;
    atomic_store_explicit(&job->worker_busy, false, memory_order_release);
    esp32_mquickjs_wifi_raw_tx_periodic_job_release(job);
    return false;
}

static void job_timer(void *opaque)
{
    (void)job_schedule(opaque);
}

esp_err_t esp32_mquickjs_wifi_raw_tx_periodic_job_new(session_t *session, payload_t *frame,
    const esp32_mquickjs_wifi_raw_tx_periodic_options_t *options, job_t **output)
{
    if (session == NULL || frame == NULL || frame->data == NULL || frame->length < ESP32_MQUICKJS_WIFI_RAW_TX_MIN_FRAME_BYTES ||
        frame->length > ESP32_MQUICKJS_WIFI_RAW_TX_MAX_FRAME_BYTES || options == NULL || output == NULL || *output != NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_raw_tx_periodic_t ledger = {0};
    if (!ledger_api(init)(&ledger, 1, options, esp_timer_get_time())) return ESP_ERR_INVALID_ARG;
    job_t *job = esp32_mquickjs_memory_wireless_calloc("wifi.raw-tx", 1, sizeof(*job), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (job == NULL) return ESP_ERR_NO_MEM;
    job->mutex = xSemaphoreCreateMutexStatic(&job->mutex_storage);
    if (job->mutex == NULL) { esp32_mquickjs_memory_payload_free(job); return ESP_ERR_NO_MEM; }
    if (!session_api(periodic_acquire)(session, frame)) {
        vSemaphoreDelete(job->mutex); esp32_mquickjs_memory_payload_free(job); return ESP_ERR_INVALID_STATE;
    }
    job->session = session; job->frame = *frame; job->ledger = ledger;
    job->references = 2; /* caller + timer/cleanup hold */
    atomic_init(&job->worker_busy, false); atomic_init(&job->retired, false);
    portENTER_CRITICAL(&s_jobs_lock);
    unsigned index = 0;
    while (index < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS && s_jobs[index] != NULL) ++index;
    bool accepted = index < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS && s_next_job_generation != 0U;
    if (accepted) {
        job->ledger.generation = s_next_job_generation;
        s_next_job_generation = s_next_job_generation == UINT32_MAX ? 0U : s_next_job_generation + 1U;
        s_jobs[index] = job;
        *frame = (payload_t){0};
        *output = job;
    }
    portEXIT_CRITICAL(&s_jobs_lock);
    if (!accepted) {
        session_api(periodic_release)(session);
        vSemaphoreDelete(job->mutex); esp32_mquickjs_memory_payload_free(job); return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void esp32_mquickjs_wifi_raw_tx_periodic_job_stop(job_t *job)
{
    if (job == NULL) return;
    xSemaphoreTake(job->mutex, portMAX_DELAY);
    job->stop_requested = true; ledger_api(stop)(&job->ledger);
    xSemaphoreGive(job->mutex);
}
void esp32_mquickjs_wifi_raw_tx_periodic_job_close(job_t *job)
{
    if (job == NULL) return;
    xSemaphoreTake(job->mutex, portMAX_DELAY);
    job->stop_requested = true; job->close_requested = true; ledger_api(close)(&job->ledger);
    xSemaphoreGive(job->mutex);
}

bool esp32_mquickjs_wifi_raw_tx_periodic_job_status(job_t *job,
    esp32_mquickjs_wifi_raw_tx_periodic_job_status_t *output)
{
    if (job == NULL || output == NULL) return false;
    xSemaphoreTake(job->mutex, portMAX_DELAY);
    *output = (esp32_mquickjs_wifi_raw_tx_periodic_job_status_t){.ledger = job->ledger,
        .ready = job->ready, .retired = atomic_load_explicit(&job->retired, memory_order_acquire),
        .stop_requested = job->stop_requested, .close_requested = job->close_requested,
        .worker_busy = atomic_load_explicit(&job->worker_busy, memory_order_acquire),
        .timer_present = job->timer != NULL, .timer_transition = job->timer_transition,
        .timer_quiesced = !job->timer_transition && (job->timer == NULL || job->timer_stopped),
        .error = job->error, .stage = job->stage, .cleanup_error = job->cleanup_error, .cleanup_stage = job->cleanup_stage};
    xSemaphoreGive(job->mutex);
    return true;
}

/* Job mutex only. SDK submission/retirement is owned by the Session worker. */
static void job_fault(job_t *job, esp_err_t error, const char *stage)
{
    if (job->error == ESP_OK) { job->error = error; job->stage = stage; }
    job->ledger.running = false; job->ledger.faulted = true;
}

static void job_observe(job_t *job)
{
    if (job->result_token.identity == 0U) return;
    if (job->result_settled) {
        if (session_api(result_release)(job->session, &job->result_token)) job->result_settled = false;
        else job_fault(job, ESP_ERR_INVALID_STATE, "result-release");
        return;
    }
    esp32_mquickjs_wifi_raw_tx_result_t result;
    if (!session_api(result_status)(job->session, &job->result_token, &result)) {
        job_fault(job, ESP_ERR_INVALID_STATE, "result-identity"); return;
    }
    if (result.driver_submitted && !job->ledger.active_submitted && !job->ledger.uncertain) {
        bool ok = ledger_api(submitted)(&job->ledger, &job->ticket); configASSERT(ok);
    }
    if (result.kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_PENDING) return;
    if (result.kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_UNCERTAIN) {
        (void)ledger_api(uncertain)(&job->ledger, &job->ticket);
        job_fault(job, result.error ? result.error : ESP_ERR_INVALID_STATE, "native-uncertain"); return;
    }
    esp32_mquickjs_wifi_raw_tx_periodic_outcome_t outcome = PERIODIC(ABORTED);
    if (result.kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_COMPLETED) {
        outcome = result.native.completion.status == ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS ? PERIODIC(SUCCESS) :
            result.native.completion.status == ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED ? PERIODIC(FAILED) : PERIODIC(UNKNOWN);
    } else if (result.kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_REJECTED) outcome = PERIODIC(REJECTED);
    else if (result.kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_DROPPED) outcome = PERIODIC(DROPPED);
    if (result.kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_TERMINATED) {
        if (!result.native.native_terminated || result.native.operation_active || !result.native.submit_returned ||
            result.native.driver_accepted != result.driver_submitted ||
            !ledger_api(terminated)(&job->ledger, &job->ticket, result.driver_submitted)) {
            job_fault(job, ESP_ERR_INVALID_STATE, "native-termination-proof"); return;
        }
        job_fault(job, result.error ? result.error : ESP_ERR_INVALID_STATE, "native-terminated");
    } else if (!ledger_api(finish)(&job->ledger, &job->ticket, outcome)) {
        job_fault(job, ESP_ERR_INVALID_STATE, "result-terminal"); return;
    }
    job->result_settled = true;
    if (session_api(result_release)(job->session, &job->result_token)) job->result_settled = false;
    else job_fault(job, ESP_ERR_INVALID_STATE, "result-release");
}

static void job_worker(void *opaque)
{
    job_t *job = opaque;
    esp_timer_handle_t timer;
    xSemaphoreTake(job->mutex, portMAX_DELAY);
    bool start = !job->ready && job->ledger.running;
    job->timer_transition = start;
    timer = job->timer;
    xSemaphoreGive(job->mutex);
    esp_err_t error = ESP_OK;
    const char *stage = NULL;
    if (start) {
        esp_timer_create_args_t args = {.callback = job_timer, .arg = job,
            .dispatch_method = ESP_TIMER_TASK, .name = "raw-tx-periodic", .skip_unhandled_events = true};
        error = esp_timer_create(&args, &timer); stage = "timer-create";
        if (error == ESP_OK) { error = esp_timer_start_periodic(timer, ESP32_MQUICKJS_WIFI_RAW_TX_SERVICE_RETRY_US); stage = "timer-start"; }
    }
    xSemaphoreTake(job->mutex, portMAX_DELAY);
    job->timer = timer;
    job->timer_transition = false;
    if (start) {
        if (error != ESP_OK) job_fault(job, error, stage);
        else job->ready = true;
    }
    esp32_mquickjs_wifi_raw_tx_session_status_t status = {0};
    if (!session_api(status)(job->session, &status)) job_fault(job, ESP_ERR_INVALID_STATE, "session-status");
    else if (status.close_requested || status.closed || status.faulted) {
        job->stop_requested = true; job->close_requested = true; ledger_api(close)(&job->ledger);
    }
    job_observe(job);
    esp32_mquickjs_wifi_raw_tx_lane_status_t lane;
    esp32_mquickjs_wifi_raw_tx_lane_status(&lane);
    bool busy = status.queued != 0U || status.active_sequence != 0U || status.worker_busy ||
        lane.active_identity != 0U || lane.waiting != 0U;
    esp32_mquickjs_wifi_raw_tx_periodic_ticket_t due_ticket = {0};
    esp32_mquickjs_wifi_raw_tx_periodic_due_t due = ledger_api(due)(&job->ledger, esp_timer_get_time(), busy, &due_ticket);
    if (due == PERIODIC(ISSUE)) job->ticket = due_ticket;
    xSemaphoreGive(job->mutex);
    payload_t copy = {0};
    if (due == PERIODIC(ISSUE)) {
        copy.length = job->frame.length;
        copy.data = esp32_mquickjs_memory_wireless_alloc("wifi.raw-tx", copy.length, ESP32_MQUICKJS_MEMORY_EXTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_TX);
        if (copy.data != NULL) memcpy(copy.data, job->frame.data, copy.length);
        xSemaphoreTake(job->mutex, portMAX_DELAY);
        if (job->stop_requested) {
            bool ok = ledger_api(finish)(&job->ledger, &job->ticket, PERIODIC(ABORTED)); configASSERT(ok);
        } else if (copy.data == NULL) {
            bool ok = ledger_api(finish)(&job->ledger, &job->ticket, PERIODIC(REJECTED)); configASSERT(ok);
            if (job->ledger.faulted) job_fault(job, ESP_ERR_NO_MEM, "packet-copy");
        } else {
            esp32_mquickjs_wifi_raw_tx_admission_t admission;
            esp32_mquickjs_wifi_raw_tx_validation_t validation;
            esp32_mquickjs_wifi_raw_tx_queue_result_t result = session_api(admit_periodic)(job->session,
                &copy, &admission, &validation, &job->result, &job->result_token);
            if (result == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FULL) {
                bool ok = ledger_api(busy)(&job->ledger, &job->ticket); configASSERT(ok);
            } else if (result != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) {
                bool ok = ledger_api(finish)(&job->ledger, &job->ticket, PERIODIC(REJECTED)); configASSERT(ok);
                if (job->ledger.faulted) job_fault(job, ESP_ERR_INVALID_STATE, "session-admission");
            }
        }
        xSemaphoreGive(job->mutex);
    }
    esp32_mquickjs_memory_payload_free(copy.data);
    (void)esp32_mquickjs_wifi_raw_tx_sessions_service();
    xSemaphoreTake(job->mutex, portMAX_DELAY);
    bool retire = ledger_api(drained)(&job->ledger) && job->result_token.identity == 0U;
    timer = job->timer;
    bool timer_stopped = job->timer_stopped;
    job->timer_transition = retire && timer != NULL;
    xSemaphoreGive(job->mutex);
    esp_err_t cleanup = ESP_OK;
    const char *cleanup_stage = NULL;
    if (retire && timer != NULL) {
        if (!timer_stopped) {
            cleanup = esp_timer_stop_blocking(timer, 1); cleanup_stage = "timer-stop";
            if (cleanup == ESP_OK) timer_stopped = true;
        }
        if (cleanup == ESP_OK) {
            cleanup = esp_timer_delete(timer); cleanup_stage = "timer-delete";
            if (cleanup == ESP_OK) timer = NULL;
        }
    }
    xSemaphoreTake(job->mutex, portMAX_DELAY);
    job->timer = timer; job->timer_stopped = timer_stopped;
    job->timer_transition = false;
    job->next_service_us = esp_timer_get_time() + (cleanup == ESP_OK ? ESP32_MQUICKJS_WIFI_RAW_TX_SERVICE_RETRY_US : ESP32_MQUICKJS_WIFI_RAW_TX_CLEANUP_RETRY_US);
    job->cleanup_error = cleanup; job->cleanup_stage = cleanup == ESP_OK ? NULL : cleanup_stage;
    session_t *released_session = NULL;
    payload_t released_frame = {0};
    if (retire && timer == NULL && cleanup == ESP_OK) {
        released_session = job->session; job->session = NULL;
        released_frame = job->frame; job->frame = (payload_t){0};
    }
    xSemaphoreGive(job->mutex);
    esp32_mquickjs_memory_payload_free(released_frame.data);
    if (released_session != NULL) {
        session_api(periodic_release)(released_session);
        /* A future close observer must not see retired before the template and
         * Session child reservation have actually been released. */
        xSemaphoreTake(job->mutex, portMAX_DELAY);
        atomic_store_explicit(&job->retired, true, memory_order_release);
        atomic_store_explicit(&job->worker_busy, false, memory_order_release);
        xSemaphoreGive(job->mutex);
    } else {
        atomic_store_explicit(&job->worker_busy, false, memory_order_release);
    }
    if (released_session != NULL) esp32_mquickjs_wifi_raw_tx_periodic_job_release(job); /* cleanup hold */
    esp32_mquickjs_wifi_raw_tx_periodic_job_release(job); /* worker; final access */
}

void esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t *output)
{
    if (output == NULL) return;
    job_t *jobs[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS];
    unsigned count = 0;
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t status = {0};
    portENTER_CRITICAL(&s_jobs_lock);
    status.identity_exhausted = s_next_job_generation == 0U;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS; ++i) {
        job_t *job = s_jobs[i];
        if (job != NULL && job->references != UINT32_MAX) { ++job->references; jobs[count++] = job; }
    }
    portEXIT_CRITICAL(&s_jobs_lock);
    for (unsigned i = 0; i < count; ++i) {
        job_t *job = jobs[i];
        xSemaphoreTake(job->mutex, portMAX_DELAY);
        bool retired = atomic_load_explicit(&job->retired, memory_order_acquire);
        if (retired) ++status.retired; else ++status.live;
        if (job->ledger.faulted) ++status.faulted;
        if (!retired && !job->ledger.running) ++status.cleanup_pending;
        if (!status.error_generation && (job->ledger.faulted || job->error || job->cleanup_error)) {
            status.error_generation = job->ledger.generation;
            status.error = job->error; status.cleanup_error = job->cleanup_error;
            status.stage = job->stage ? job->stage : job->ledger.exhausted ? "schedule-exhausted" :
                job->ledger.faulted ? "schedule-or-result" : NULL;
            status.cleanup_stage = job->cleanup_stage;
        }
        xSemaphoreGive(job->mutex);
        esp32_mquickjs_wifi_raw_tx_periodic_job_release(job);
    }
    *output = status;
}

bool esp32_mquickjs_wifi_raw_tx_periodic_jobs_service(void)
{
    job_t *jobs[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS];
    unsigned count = 0;
    portENTER_CRITICAL(&s_jobs_lock);
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS; ++i) {
        job_t *job = s_jobs[i];
        if (job != NULL && job->references != UINT32_MAX && !atomic_load_explicit(&job->retired, memory_order_acquire)) {
            ++job->references; jobs[count++] = job;
        }
    }
    portEXIT_CRITICAL(&s_jobs_lock);
    bool handled = false;
    for (unsigned i = 0; i < count; ++i) {
        job_t *job = jobs[i];
        if (xSemaphoreTake(job->mutex, 0) == pdTRUE) {
            bool needs_service = !job->ready || job->timer_stopped || job->timer == NULL ||
                job->stop_requested || job->close_requested;
            if (needs_service && esp_timer_get_time() >= job->next_service_us && job_schedule(job)) handled = true;
            xSemaphoreGive(job->mutex);
        }
        esp32_mquickjs_wifi_raw_tx_periodic_job_release(job);
    }
    return handled;
}
#endif
