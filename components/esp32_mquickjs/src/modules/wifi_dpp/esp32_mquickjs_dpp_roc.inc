/* Fixed SDK ROC calls done_cb synchronously on the Wi-Fi task, before clearing
 * its off-channel record. Capture only here; never submit the next operation
 * from inside that callback. Its return and the native record's retirement
 * precede reuse. The 8-bit driver ID is not an eloop operation identity. */
#include "freertos/FreeRTOS.h"

struct esp32qjs_dpp_roc {
    uint64_t ticket;
    uint32_t generation, duplicates, ignored;
    esp_err_t error;
    wifi_roc_done_status_t status;
    uint8_t op_id, completed_id, channel;
    bool held, returned, complete, cancel_requested, poll_queued;
};
static struct esp32qjs_dpp_roc s_dpp_roc;
static uint64_t s_dpp_roc_last_ticket;
static portMUX_TYPE s_dpp_roc_lock = portMUX_INITIALIZER_UNLOCKED;
static void esp32qjs_dpp_roc_poll(void *, void *);
static void dpp_abort_failure_locked(uint32_t);

static struct esp32qjs_dpp_roc esp32qjs_dpp_roc_snapshot(void)
{
    portENTER_CRITICAL(&s_dpp_roc_lock);
    struct esp32qjs_dpp_roc snapshot = s_dpp_roc;
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    return snapshot;
}

static bool esp32qjs_dpp_roc_held(void)
{ return esp32qjs_dpp_roc_snapshot().held; }

static void esp32qjs_dpp_roc_status_locked(esp32_mquickjs_wifi_dpp_result_status_t *out)
{
    struct esp32qjs_dpp_roc snapshot = esp32qjs_dpp_roc_snapshot();
    /* The boot-owned numeric counter persists, but another Session's last ROC
     * is not part of this Session's diagnostics. */
    if (snapshot.generation != s_dpp_generation || !s_dpp_result->ever_bound) return;
    out->roc_operation = snapshot.ticket;
    out->roc_error = snapshot.error;
    out->roc_held = snapshot.held;
    out->roc_completed = snapshot.complete;
    out->roc_duplicates = snapshot.duplicates;
    out->roc_ignored = snapshot.ignored;
}

static void esp32qjs_dpp_roc_done(uint32_t context, uint8_t op_id, wifi_roc_done_status_t status)
{
    bool native_task = current_task_is_wifi_task();
    portENTER_CRITICAL(&s_dpp_roc_lock);
    if (!s_dpp_roc.held || !native_task || context != (uint32_t)(uintptr_t)s_action_rx_cb ||
        (s_dpp_roc.returned && op_id != s_dpp_roc.op_id)) {
        esp32qjs_dpp_result_count(&s_dpp_roc.ignored);
    } else if (s_dpp_roc.complete) {
        esp32qjs_dpp_result_count(&s_dpp_roc.duplicates);
    } else {
        s_dpp_roc.completed_id = op_id;
        s_dpp_roc.status = status;
        s_dpp_roc.complete = true;
        if (status != WIFI_ROC_DONE && status != WIFI_ROC_FAIL)
            s_dpp_roc.error = ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    /* No allocation, SDK call, API mutex, queue post or protocol callback. */
}

static esp_err_t esp32qjs_dpp_roc_schedule_locked(uint64_t ticket)
{
    portENTER_CRITICAL(&s_dpp_roc_lock);
    bool exact = s_dpp_roc.held && s_dpp_roc.ticket == ticket;
    bool queued = exact && s_dpp_roc.poll_queued;
    if (exact) s_dpp_roc.poll_queued = true;
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    if (!exact) return ESP_ERR_INVALID_STATE;
    if (queued) return ESP_OK;
    if (esp32qjs_dpp_async_register(0, 10000, esp32qjs_dpp_roc_poll,
        (void *)(uintptr_t)(ticket >> 32), (void *)(uintptr_t)(uint32_t)ticket) == 0) return ESP_OK;
    portENTER_CRITICAL(&s_dpp_roc_lock);
    if (s_dpp_roc.ticket == ticket) s_dpp_roc.poll_queued = false;
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    return ESP_ERR_NO_MEM;
}

/* Called on the Wi-Fi task, after the producer and submit/cancel have returned.
 * A foreign nonzero native record is not used as retirement evidence. The
 * default observation event loop is deliberately absent from this path. */
static esp_err_t esp32qjs_dpp_roc_reap_locked(uint64_t ticket, bool *restart, esp_err_t *outcome)
{
    *restart = false;
    *outcome = ESP_OK;
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    struct esp32qjs_dpp_roc snapshot = esp32qjs_dpp_roc_snapshot();
    if (!snapshot.held) return ESP_OK;
    if (snapshot.ticket != ticket || !snapshot.returned) return ESP_ERR_NOT_FINISHED;
    esp_err_t error = esp32_mquickjs_wifi_action_sdk_quiescent();
    if (error != ESP_OK) return error;
    *outcome = snapshot.error;
    if (!snapshot.cancel_requested && !snapshot.complete && *outcome == ESP_OK)
        *outcome = ESP_ERR_INVALID_STATE;
    esp32qjs_dpp_async_cancel(esp32qjs_dpp_roc_poll,
        (void *)(uintptr_t)(ticket >> 32), (void *)(uintptr_t)(uint32_t)ticket);
    portENTER_CRITICAL(&s_dpp_roc_lock);
    s_dpp_roc.held = false;
    s_dpp_roc.poll_queued = false;
    s_dpp_roc.error = *outcome;
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    s_dpp_roc_submitted = false;
    s_dpp_roc_cancelled = false;
    atomic_store(&roc_in_progress, false);
    if (s_dpp_event_group) os_event_group_set_bits(s_dpp_event_group, DPP_ROC_EVENT_HANDLED);
    *restart = *outcome == ESP_OK && !snapshot.cancel_requested &&
        snapshot.generation == s_dpp_generation && atomic_load(&s_dpp_init_done) &&
        !atomic_load(&dpp_shutting_down) && s_dpp_ctx.dpp_listen_ongoing &&
        s_dpp_result && !s_dpp_result->status.terminal && !s_dpp_result->status.closing;
    return ESP_OK;
}

static esp_err_t esp32qjs_dpp_roc_submit_locked(wifi_roc_req_t *request)
{
    if (!current_task_is_wifi_task() || !s_dpp_result || s_dpp_result->status.closing ||
        s_dpp_result->status.terminal || !atomic_load(&s_dpp_init_done)) return ESP_ERR_INVALID_STATE;
    struct esp32qjs_dpp_roc previous = esp32qjs_dpp_roc_snapshot();
    if (previous.held) {
        bool restart;
        esp_err_t outcome;
        esp_err_t error = esp32qjs_dpp_roc_reap_locked(previous.ticket, &restart, &outcome);
        if (error != ESP_OK) return error;
        if (outcome != ESP_OK) return outcome;
    }
    esp_err_t error = esp32_mquickjs_wifi_action_sdk_quiescent();
    if (error != ESP_OK) return error;
    if (s_dpp_roc_last_ticket == UINT64_MAX) return ESP_ERR_NO_MEM;
    uint64_t ticket = ++s_dpp_roc_last_ticket;
    portENTER_CRITICAL(&s_dpp_roc_lock);
    s_dpp_roc = (struct esp32qjs_dpp_roc){.ticket = ticket, .generation = s_dpp_generation,
        .channel = request->channel, .held = true};
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    /* Reserve the dispatcher before starting native work. A synchronous
     * completion may arrive before the driver writes/returns its op_id. */
    error = esp32qjs_dpp_roc_schedule_locked(ticket);
    if (error != ESP_OK) {
        portENTER_CRITICAL(&s_dpp_roc_lock);
        s_dpp_roc.held = false;
        s_dpp_roc.error = error;
        portEXIT_CRITICAL(&s_dpp_roc_lock);
        return error;
    }
    request->done_cb = esp32qjs_dpp_roc_done;
    s_dpp_roc_submitted = true;
    s_dpp_roc_cancelled = false;
    atomic_store(&roc_in_progress, true);
    if (s_dpp_event_group) os_event_group_clear_bits(s_dpp_event_group, DPP_ROC_EVENT_HANDLED);
    error = esp_wifi_remain_on_channel(request);
    s_dpp_result->status.roc_submit_error = error;
    portENTER_CRITICAL(&s_dpp_roc_lock);
    s_dpp_roc.op_id = request->op_id;
    s_dpp_roc.returned = true;
    if (error != ESP_OK) s_dpp_roc.error = error;
    else if (s_dpp_roc.complete && s_dpp_roc.completed_id != request->op_id)
        s_dpp_roc.error = ESP_ERR_INVALID_STATE;
    esp_err_t result = s_dpp_roc.error;
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    /* Even a failed submission retains its record until native retirement. */
    return result;
}

static esp_err_t esp32qjs_dpp_roc_cancel_locked(void)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    struct esp32qjs_dpp_roc snapshot = esp32qjs_dpp_roc_snapshot();
    if (!snapshot.held) return ESP_OK;
    if (!snapshot.returned) return ESP_ERR_NOT_FINISHED;
    portENTER_CRITICAL(&s_dpp_roc_lock);
    s_dpp_roc.cancel_requested = true;
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    bool restart;
    esp_err_t outcome;
    if (esp32qjs_dpp_roc_reap_locked(snapshot.ticket, &restart, &outcome) == ESP_OK) return ESP_OK;
    esp_err_t error = ESP_OK;
    if (!s_dpp_roc_cancelled) {
        wifi_roc_req_t request = {.ifx = WIFI_IF_STA, .type = WIFI_ROC_CANCEL,
            .rx_cb = s_action_rx_cb, .channel = snapshot.channel, .op_id = snapshot.op_id,
            .sec_channel = WIFI_SECOND_CHAN_NONE};
        error = esp_wifi_remain_on_channel(&request);
        if (s_dpp_result) s_dpp_result->status.roc_cancel_error = error;
        if (error == ESP_OK) s_dpp_roc_cancelled = true;
    }
    esp_err_t retired = esp32qjs_dpp_roc_reap_locked(snapshot.ticket, &restart, &outcome);
    if (retired == ESP_OK) return ESP_OK;
    return error != ESP_OK ? error : retired;
}

static void esp32qjs_dpp_roc_poll(void *high, void *low)
{
    uint64_t ticket = ((uint64_t)(uintptr_t)high << 32) | (uint32_t)(uintptr_t)low;
    if (!current_task_is_wifi_task() || dpp_api_lock() != ESP_OK) return;
    struct esp32qjs_dpp_roc snapshot = esp32qjs_dpp_roc_snapshot();
    if (!snapshot.held || snapshot.ticket != ticket || snapshot.generation != s_dpp_generation ||
        atomic_load(&dpp_shutting_down)) { dpp_api_unlock(); return; }
    esp_err_t timer_error = esp32qjs_wifi_chm_timer_service_native();
    if (timer_error != ESP_OK) { dpp_abort_failure_locked(timer_error); return; }
    portENTER_CRITICAL(&s_dpp_roc_lock);
    s_dpp_roc.poll_queued = false;
    portEXIT_CRITICAL(&s_dpp_roc_lock);
    bool restart;
    esp_err_t outcome;
    esp_err_t error = esp32qjs_dpp_roc_reap_locked(ticket, &restart, &outcome);
    if (error == ESP_ERR_TIMEOUT || error == ESP_ERR_NOT_FINISHED)
        error = esp32qjs_dpp_roc_schedule_locked(ticket);
    if (error != ESP_OK || outcome != ESP_OK) {
        dpp_abort_failure_locked(error != ESP_OK ? error : outcome);
        return;
    }
    dpp_api_unlock();
    if (restart) dpp_listen_next_channel(NULL, NULL);
}
