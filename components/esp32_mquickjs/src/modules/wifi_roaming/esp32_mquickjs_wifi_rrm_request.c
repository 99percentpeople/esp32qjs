#include "esp32_mquickjs_wifi_rrm_request.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_RRM_SUPPORT
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>

struct esp32_mquickjs_wifi_rrm_request {
    uint32_t references;
    esp32_mquickjs_wifi_radio_operation_t token;
    esp32_mquickjs_wifi_rrm_status_t status;
    uint8_t *report;
    int64_t next_retry_us;
    bool busy, callback_busy, observation_busy;
    uint32_t waiters;
    struct esp32_mquickjs_wifi_rrm_request *observation_next;
};
static portMUX_TYPE s_rrm_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_rrm_request_t *s_rrm_active;
static esp32_mquickjs_wifi_rrm_request_t *s_rrm_observations;
static uint32_t s_rrm_handles, s_rrm_reserved_bytes;

static void rrm_free_report(uint8_t *report, unsigned bytes)
{
    if (report == NULL) return;
    esp32_mquickjs_memory_payload_free(report);
    portENTER_CRITICAL(&s_rrm_lock);
    s_rrm_reserved_bytes -= bytes;
    portEXIT_CRITICAL(&s_rrm_lock);
}
static uint8_t *rrm_detach_report_locked(esp32_mquickjs_wifi_rrm_request_t *r, unsigned *bytes)
{
    *bytes = 0;
    if (r->busy || r->callback_busy || r->observation_busy) return NULL;
    if (!r->status.closed && (!r->status.retired || r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_REPORT)) return NULL;
    uint8_t *report = r->report;
    *bytes = r->status.retained_bytes;
    r->report = NULL;
    r->status.retained_bytes = 0;
    return report;
}
bool esp32_mquickjs_wifi_rrm_retain(esp32_mquickjs_wifi_rrm_request_t *r)
{
    if (r == NULL) return false;
    portENTER_CRITICAL(&s_rrm_lock);
    bool ok = r->references && r->references != UINT32_MAX;
    if (ok) ++r->references;
    portEXIT_CRITICAL(&s_rrm_lock);
    return ok;
}
void esp32_mquickjs_wifi_rrm_release(esp32_mquickjs_wifi_rrm_request_t *r)
{
    if (r == NULL) return;
    portENTER_CRITICAL(&s_rrm_lock);
    bool destroy = --r->references == 0U;
    portEXIT_CRITICAL(&s_rrm_lock);
    if (!destroy) return;
    rrm_free_report(r->report, r->status.retained_bytes);
    esp32_mquickjs_memory_payload_free(r);
    portENTER_CRITICAL(&s_rrm_lock);
    --s_rrm_handles;
    portEXIT_CRITICAL(&s_rrm_lock);
}
esp_err_t esp32_mquickjs_wifi_rrm_create(unsigned capacity, esp32_mquickjs_wifi_rrm_request_t **output)
{
    if (output == NULL || *output != NULL || capacity == 0U || capacity > ESP32_MQUICKJS_WIFI_RRM_MAX_REPORT_BYTES)
        return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_rrm_lock);
    bool admitted = s_rrm_handles < ESP32_MQUICKJS_WIFI_RRM_MAX_HANDLES &&
        capacity <= ESP32_MQUICKJS_WIFI_RRM_MAX_RETAINED_BYTES - s_rrm_reserved_bytes;
    if (admitted) { ++s_rrm_handles; s_rrm_reserved_bytes += capacity; }
    portEXIT_CRITICAL(&s_rrm_lock);
    if (!admitted) return ESP_ERR_NO_MEM;
    esp32_mquickjs_wifi_rrm_request_t *r = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*r), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    uint8_t *report = r != NULL ? esp32_mquickjs_memory_wireless_alloc("wifi", capacity, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_POOL) : NULL;
    if (report == NULL) {
        if (r != NULL) esp32_mquickjs_memory_payload_free(r);
        portENTER_CRITICAL(&s_rrm_lock);
        --s_rrm_handles; s_rrm_reserved_bytes -= capacity;
        portEXIT_CRITICAL(&s_rrm_lock);
        return ESP_ERR_NO_MEM;
    }
    r->references = 1;
    r->report = report;
    r->status.capacity = r->status.retained_bytes = (uint16_t)capacity;
    r->status.submit.ownership = r->status.cancel.ownership = r->status.cleanup.ownership = -1;
    *output = r;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_rrm_start(esp32_mquickjs_wifi_rrm_request_t *r)
{
    if (r == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_rrm_lock);
    bool ok = !s_rrm_active && !r->status.started && !r->status.closed &&
        r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_PENDING && r->references <= UINT32_MAX - 2U;
    if (ok) {
        r->references += 2U; /* Active registry and observation queue. */
        s_rrm_active = r; r->status.started = true;
        esp32_mquickjs_wifi_rrm_request_t **tail = &s_rrm_observations;
        while (*tail != NULL) tail = &(*tail)->observation_next;
        *tail = r;
    }
    portEXIT_CRITICAL(&s_rrm_lock);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static void rrm_cancel_locked(esp32_mquickjs_wifi_rrm_request_t *r, bool timeout)
{
    if (r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_PENDING) {
        r->status.terminal = timeout ? ESP32_MQUICKJS_WIFI_RRM_TIMED_OUT : ESP32_MQUICKJS_WIFI_RRM_CANCELLED;
        r->status.error = timeout ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
        r->status.stage = timeout ? "request-timeout" : "request-cancel";
    }
    if (!r->status.started) r->status.retired = true;
    r->next_retry_us = 0;
}
void esp32_mquickjs_wifi_rrm_cancel(esp32_mquickjs_wifi_rrm_request_t *r, bool timeout)
{
    if (r == NULL) return;
    portENTER_CRITICAL(&s_rrm_lock);
    rrm_cancel_locked(r, timeout);
    portEXIT_CRITICAL(&s_rrm_lock);
}
void esp32_mquickjs_wifi_rrm_close(esp32_mquickjs_wifi_rrm_request_t *r)
{
    if (r == NULL) return;
    unsigned bytes;
    portENTER_CRITICAL(&s_rrm_lock);
    rrm_cancel_locked(r, false);
    r->status.closed = true;
    uint8_t *report = rrm_detach_report_locked(r, &bytes);
    portEXIT_CRITICAL(&s_rrm_lock);
    rrm_free_report(report, bytes);
}
bool esp32_mquickjs_wifi_rrm_status(esp32_mquickjs_wifi_rrm_request_t *r, esp32_mquickjs_wifi_rrm_status_t *output)
{
    if (r == NULL || output == NULL) return false;
    portENTER_CRITICAL(&s_rrm_lock);
    *output = r->status;
    portEXIT_CRITICAL(&s_rrm_lock);
    return true;
}
bool esp32_mquickjs_wifi_rrm_copy(esp32_mquickjs_wifi_rrm_request_t *r, size_t offset, void *output, size_t length)
{
    if (r == NULL || output == NULL || length > ESP32_MQUICKJS_WIFI_RRM_COPY_BYTES) return false;
    portENTER_CRITICAL(&s_rrm_lock);
    bool ok = r->status.retired && !r->status.closed && r->report != NULL &&
        r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_REPORT &&
        offset <= r->status.received_bytes && length <= r->status.received_bytes - offset;
    if (ok) memcpy(output, r->report + offset, length);
    portEXIT_CRITICAL(&s_rrm_lock);
    return ok;
}

static void rrm_callback(void *identity, const uint8_t *report, size_t length)
{
    portENTER_CRITICAL(&s_rrm_lock);
    esp32_mquickjs_wifi_rrm_request_t *r = s_rrm_active;
    bool exact = r != NULL && r->token.identity != 0U &&
        (uintptr_t)identity == r->token.identity && !r->callback_busy && r->references != UINT32_MAX;
    if (!exact) { portEXIT_CRITICAL(&s_rrm_lock); return; }
    ++r->references;
    r->callback_busy = true;
    bool capture = r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_PENDING && !r->status.closed;
    bool valid = report != NULL && length != 0U && length <= r->status.retained_bytes && r->report != NULL;
    uint8_t *destination = r->report;
    portEXIT_CRITICAL(&s_rrm_lock);
    /* The registry/callback reference and callback_busy pin this span. No
     * allocation, JS, Radio mutex or observer queue work on the SDK task. */
    if (capture && valid) memcpy(destination, report, length);
    portENTER_CRITICAL(&s_rrm_lock);
    r->status.callback_seen = true;
    if (r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_PENDING) {
        r->status.received_bytes = length;
        if (report == NULL && length == 0U) {
            r->status.terminal = ESP32_MQUICKJS_WIFI_RRM_NO_REPORT;
            r->status.error = ESP_ERR_INVALID_RESPONSE;
            r->status.stage = "native-no-report";
        } else if (valid && capture) {
            r->status.terminal = ESP32_MQUICKJS_WIFI_RRM_REPORT;
        } else {
            r->status.terminal = ESP32_MQUICKJS_WIFI_RRM_FAILED;
            r->status.error = ESP_ERR_INVALID_SIZE;
            r->status.stage = "native-report-size";
        }
    }
    r->callback_busy = false;
    r->next_retry_us = 0;
    portEXIT_CRITICAL(&s_rrm_lock);
    /* SDK clears its pointer only after this return. The registry remains
     * live until a later Radio QUERY proves that has happened. Observation
     * publication is intentionally left to the future public completion path. */
    esp32_mquickjs_wifi_rrm_release(r);
}

static void rrm_fail_locked(esp32_mquickjs_wifi_rrm_request_t *r, esp_err_t error, const char *stage)
{
    if (r->status.terminal != ESP32_MQUICKJS_WIFI_RRM_PENDING) return;
    r->status.terminal = ESP32_MQUICKJS_WIFI_RRM_FAILED;
    r->status.error = error;
    r->status.stage = stage;
}
static void rrm_worker(void *opaque)
{
    esp32_mquickjs_wifi_rrm_request_t *r = opaque;
    portENTER_CRITICAL(&s_rrm_lock);
    esp32_mquickjs_wifi_radio_operation_t token = r->token;
    bool first = !r->status.worker_started && r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_PENDING;
    r->status.worker_started = true;
    portEXIT_CRITICAL(&s_rrm_lock);
    if (first) {
        esp_err_t error = esp32_mquickjs_wifi_rrm_reserve(&token);
        portENTER_CRITICAL(&s_rrm_lock);
        r->token = token;
        r->status.operation = token;
        if (error != ESP_OK) rrm_fail_locked(r, error, "radio-admission");
        bool submit = error == ESP_OK && r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_PENDING;
        portEXIT_CRITICAL(&s_rrm_lock);
        if (submit) {
            esp32_mquickjs_wifi_rrm_sdk_result_t result;
            error = esp32_mquickjs_wifi_radio_rrm_command(&token, ESP32_MQUICKJS_WIFI_RRM_SUBMIT, rrm_callback, &result);
            portENTER_CRITICAL(&s_rrm_lock);
            r->status.submit = result;
            if (error != ESP_OK || result.code != 0)
                rrm_fail_locked(r, error != ESP_OK ? error : ESP_FAIL, "neighbor-request-submit");
            portEXIT_CRITICAL(&s_rrm_lock);
        }
    }
    esp32_mquickjs_wifi_rrm_sdk_result_t cleanup = {.ownership = -1};
    esp_err_t cleanup_error = ESP_OK;
    const char *cleanup_stage = NULL;
    if (token.identity != 0U) {
        portENTER_CRITICAL(&s_rrm_lock);
        bool cancel = r->status.terminal != ESP32_MQUICKJS_WIFI_RRM_PENDING &&
            !r->status.callback_seen && !r->status.cancel_written;
        portEXIT_CRITICAL(&s_rrm_lock);
        if (cancel) {
            cleanup_error = esp32_mquickjs_wifi_radio_rrm_command(&token,
                ESP32_MQUICKJS_WIFI_RRM_CANCEL, rrm_callback, &cleanup);
            portENTER_CRITICAL(&s_rrm_lock);
            r->status.cancel = cleanup;
            r->status.cancel_error = cleanup_error;
            portEXIT_CRITICAL(&s_rrm_lock);
            if (cleanup_error == ESP_OK && cleanup.code == 0 && cleanup.ownership == 0) {
                portENTER_CRITICAL(&s_rrm_lock);
                r->status.cancel_written = true;
                portEXIT_CRITICAL(&s_rrm_lock);
            } else {
                if (cleanup_error == ESP_OK) cleanup_error = ESP_FAIL;
                cleanup_stage = "neighbor-request-cancel";
            }
        }
        /* A successful query is independent proof, including after an uncertain
         * cancellation. No resend; only the unfinished cleanup suffix retries. */
        esp_err_t retired = esp32_mquickjs_wifi_radio_rrm_retire(&token, rrm_callback, &cleanup);
        if (retired == ESP_OK) { cleanup_error = ESP_OK; cleanup_stage = NULL; }
        else if (cleanup_error == ESP_OK && !(cleanup.entered && cleanup.ownership == 1)) {
            cleanup_error = retired; cleanup_stage = "neighbor-request-retire";
        }
    }
    unsigned bytes;
    int64_t retry = esp_timer_get_time() + 100000;
    portENTER_CRITICAL(&s_rrm_lock);
    r->token = token;
    r->status.cleanup = cleanup;
    r->status.cleanup_error = cleanup_error;
    r->status.cleanup_stage = cleanup_stage;
    r->status.retired = token.identity == 0U;
    if (r->status.retired) rrm_fail_locked(r, ESP_ERR_INVALID_RESPONSE, "native-retired-without-report");
    r->busy = false;
    r->next_retry_us = retry;
    uint8_t *report = rrm_detach_report_locked(r, &bytes);
    bool detach = r->status.retired && s_rrm_active == r;
    if (detach) s_rrm_active = NULL;
    portEXIT_CRITICAL(&s_rrm_lock);
    rrm_free_report(report, bytes);
    if (detach) esp32_mquickjs_wifi_rrm_release(r);
    esp32_mquickjs_wifi_rrm_release(r);
}
bool esp32_mquickjs_wifi_rrm_service(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_rrm_lock);
    esp32_mquickjs_wifi_rrm_request_t *r = s_rrm_active;
    bool run = r != NULL && !r->busy && r->references != UINT32_MAX && now >= r->next_retry_us;
    if (run) { r->busy = true; ++r->references; }
    portEXIT_CRITICAL(&s_rrm_lock);
    if (!run) return false;
    if (esp32_mquickjs_submit_background_worker(rrm_worker, r)) return true;
    unsigned bytes;
    portENTER_CRITICAL(&s_rrm_lock);
    r->busy = false;
    r->next_retry_us = now + 100000;
    r->status.cleanup_error = ESP_ERR_NO_MEM;
    r->status.cleanup_stage = "neighbor-request-worker-queue";
    uint8_t *report = rrm_detach_report_locked(r, &bytes);
    portEXIT_CRITICAL(&s_rrm_lock);
    rrm_free_report(report, bytes);
    esp32_mquickjs_wifi_rrm_release(r);
    return false;
}
bool esp32_mquickjs_wifi_rrm_prepare_runtime_destroy(void)
{
    portENTER_CRITICAL(&s_rrm_lock);
    if (s_rrm_active != NULL) {
        rrm_cancel_locked(s_rrm_active, false);
        s_rrm_active->status.closed = true;
    }
    portEXIT_CRITICAL(&s_rrm_lock);
    (void)esp32_mquickjs_wifi_rrm_service();
    (void)esp32_mquickjs_wifi_rrm_poll_observations(true);
    portENTER_CRITICAL(&s_rrm_lock);
    bool drained = s_rrm_active == NULL;
    portEXIT_CRITICAL(&s_rrm_lock);
    return drained;
}
void esp32_mquickjs_wifi_rrm_counts(esp32_mquickjs_wifi_rrm_counts_t *output)
{
    if (output == NULL) return;
    portENTER_CRITICAL(&s_rrm_lock);
    *output = (esp32_mquickjs_wifi_rrm_counts_t){s_rrm_handles, s_rrm_reserved_bytes,
        s_rrm_active != NULL, s_rrm_active != NULL && s_rrm_active->busy,
        s_rrm_active != NULL && s_rrm_active->callback_busy};
    portEXIT_CRITICAL(&s_rrm_lock);
}

bool esp32_mquickjs_wifi_rrm_waiter_add(esp32_mquickjs_wifi_rrm_request_t *r)
{
    if (r == NULL) return false;
    portENTER_CRITICAL(&s_rrm_lock);
    bool ok = r->waiters != UINT32_MAX;
    if (ok) ++r->waiters;
    portEXIT_CRITICAL(&s_rrm_lock);
    return ok;
}
bool esp32_mquickjs_wifi_rrm_current_status(esp32_mquickjs_wifi_rrm_status_t *output)
{
    if (output == NULL) return false;
    portENTER_CRITICAL(&s_rrm_lock);
    bool active = s_rrm_active != NULL;
    if (active) *output = s_rrm_active->status;
    portEXIT_CRITICAL(&s_rrm_lock);
    return active;
}
void esp32_mquickjs_wifi_rrm_waiter_remove(esp32_mquickjs_wifi_rrm_request_t *r)
{
    if (r == NULL) return;
    portENTER_CRITICAL(&s_rrm_lock);
    if (r->waiters != 0U) --r->waiters;
    portEXIT_CRITICAL(&s_rrm_lock);
}
bool esp32_mquickjs_wifi_rrm_poll_observations(bool discard)
{
    bool handled = false;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RRM_MAX_HANDLES; ++i) {
        portENTER_CRITICAL(&s_rrm_lock);
        esp32_mquickjs_wifi_rrm_request_t **link = &s_rrm_observations;
        while (*link != NULL && !discard && (!(*link)->status.retired || (*link)->waiters != 0U))
            link = &(*link)->observation_next;
        esp32_mquickjs_wifi_rrm_request_t *r = *link;
        if (r == NULL) { portEXIT_CRITICAL(&s_rrm_lock); break; }
        *link = r->observation_next;
        r->observation_next = NULL;
        r->observation_busy = true;
        bool publish = !discard && !r->status.closed && r->status.callback_seen &&
            (r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_NO_REPORT ||
             (r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_REPORT && r->report != NULL));
        const uint8_t *report = r->status.terminal == ESP32_MQUICKJS_WIFI_RRM_REPORT ? r->report : NULL;
        size_t length = report != NULL ? r->status.received_bytes : 0;
        portEXIT_CRITICAL(&s_rrm_lock);
        if (publish) esp32qjs_rrm_publish_observation(report, length);
        unsigned bytes;
        portENTER_CRITICAL(&s_rrm_lock);
        r->observation_busy = false;
        uint8_t *released = rrm_detach_report_locked(r, &bytes);
        portEXIT_CRITICAL(&s_rrm_lock);
        rrm_free_report(released, bytes);
        esp32_mquickjs_wifi_rrm_release(r);
        handled = true;
    }
    return handled;
}
#endif
