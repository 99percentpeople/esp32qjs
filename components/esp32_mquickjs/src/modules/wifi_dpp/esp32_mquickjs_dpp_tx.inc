/* The native publisher captures into one bounded record before observation.
 * TX DONE/FAILED and dwell completion are independent. The scheduler carries
 * a non-reusable numeric ticket; no SDK 8-bit ID crosses an async boundary. */
struct esp32qjs_dpp_tx {
    uint64_t ticket;
    uint32_t generation, duplicates, ignored;
    esp_err_t error;
    enum dpp_tx_frame_type type;
    wifi_action_tx_status_type_t status;
    uint8_t op_id, captured_id, channel;
    bool held, returned, captured, completed, duration_completed;
    bool delivered, cancel_requested, poll_queued;
};
static struct esp32qjs_dpp_tx s_dpp_tx;
static portMUX_TYPE s_dpp_tx_lock = portMUX_INITIALIZER_UNLOCKED;
/* One next protocol frame may wait for the previous EB's recycler. Its copied
 * request has no borrowed auth/wpabuf/JS storage and no native op_id yet. */
struct esp32qjs_dpp_tx_deferred {
    uint64_t ticket;
    uint32_t generation;
    enum dpp_tx_frame_type type;
    wifi_action_tx_req_t request;
};
_Static_assert(offsetof(wifi_action_tx_req_t, data) == sizeof(wifi_action_tx_req_t),
    "review DPP appended action payload layout");
_Static_assert(offsetof(struct esp32qjs_dpp_tx_deferred, request) + sizeof(wifi_action_tx_req_t) ==
    sizeof(struct esp32qjs_dpp_tx_deferred), "review deferred DPP request tail padding");
static struct esp32qjs_dpp_tx_deferred *s_dpp_tx_deferred;
static void esp32qjs_dpp_tx_poll(void *, void *);
static void esp32qjs_dpp_tx_deferred_poll(void *, void *);
static void esp32qjs_dpp_tx_protocol_result(enum dpp_tx_frame_type, wifi_action_tx_status_type_t);

static struct esp32qjs_dpp_tx esp32qjs_dpp_tx_snapshot(void)
{
    portENTER_CRITICAL(&s_dpp_tx_lock);
    struct esp32qjs_dpp_tx snapshot = s_dpp_tx;
    portEXIT_CRITICAL(&s_dpp_tx_lock);
    return snapshot;
}

static bool esp32qjs_dpp_tx_held(void)
{ return esp32qjs_dpp_tx_snapshot().held || s_dpp_tx_deferred != NULL; }

static void esp32qjs_dpp_tx_buffer_status(esp32_mquickjs_wifi_dpp_result_status_t *out,
    const esp32qjs_wifi_offchan_tx_status_t *buffer)
{
    out->tx_buffer_error = buffer->error; out->tx_post_error = buffer->post_error;
    out->tx_wake_retries = buffer->wake_retries;
    out->tx_buffer_present = buffer->buffer_present;
    out->tx_recycling = buffer->recycling; out->tx_recycled = buffer->recycled;
}

static void esp32qjs_dpp_tx_status_locked(esp32_mquickjs_wifi_dpp_result_status_t *out)
{
    struct esp32qjs_dpp_tx snapshot = esp32qjs_dpp_tx_snapshot();
    out->tx_queued = s_dpp_tx_deferred != NULL;
    out->tx_queued_bytes = s_dpp_tx_deferred ? sizeof(*s_dpp_tx_deferred) + s_dpp_tx_deferred->request.data_len : 0;
    out->reserved_bytes += out->tx_queued_bytes;
    if (snapshot.generation != s_dpp_generation || !s_dpp_result->ever_bound) return;
    out->tx_operation = snapshot.ticket;
    out->tx_error = snapshot.error;
    out->tx_duplicates = snapshot.duplicates;
    out->tx_ignored = snapshot.ignored;
    out->tx_held = snapshot.held;
    out->tx_completed = snapshot.completed;
    out->tx_duration_completed = snapshot.duration_completed;
    esp32qjs_wifi_offchan_tx_status_t buffer;
    if (snapshot.held && esp32qjs_wifi_offchan_tx_status(snapshot.ticket, &buffer) == ESP_OK)
        esp32qjs_dpp_tx_buffer_status(out, &buffer);
}

void esp32qjs_dpp_tx_status_capture(const wifi_event_action_tx_status_t *event)
{
    if (!event) return;
    bool native_task = current_task_is_wifi_task();
    portENTER_CRITICAL(&s_dpp_tx_lock);
    if (!s_dpp_tx.held || !native_task || event->context != (uint32_t)(uintptr_t)s_action_rx_cb ||
        event->ifx != WIFI_IF_STA || event->channel != s_dpp_tx.channel ||
        (s_dpp_tx.returned && event->op_id != s_dpp_tx.op_id)) {
        esp32qjs_dpp_result_count(&s_dpp_tx.ignored);
    } else if (s_dpp_tx.captured && event->op_id != s_dpp_tx.captured_id) {
        s_dpp_tx.error = ESP_ERR_INVALID_STATE;
    } else {
        s_dpp_tx.captured = true;
        s_dpp_tx.captured_id = event->op_id;
        if (event->status == WIFI_ACTION_TX_DONE || event->status == WIFI_ACTION_TX_FAILED) {
            if (s_dpp_tx.completed) esp32qjs_dpp_result_count(&s_dpp_tx.duplicates);
            else { s_dpp_tx.status = event->status; s_dpp_tx.completed = true; }
        } else if (event->status == WIFI_ACTION_TX_DURATION_COMPLETED || event->status == WIFI_ACTION_TX_OP_CANCELLED) {
            if (s_dpp_tx.duration_completed) esp32qjs_dpp_result_count(&s_dpp_tx.duplicates);
            s_dpp_tx.duration_completed = true;
        } else s_dpp_tx.error = ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_dpp_tx_lock);
}

static esp_err_t esp32qjs_dpp_tx_schedule_locked(uint64_t ticket)
{
    portENTER_CRITICAL(&s_dpp_tx_lock);
    bool exact = s_dpp_tx.held && s_dpp_tx.ticket == ticket;
    bool queued = exact && s_dpp_tx.poll_queued;
    if (exact) s_dpp_tx.poll_queued = true;
    portEXIT_CRITICAL(&s_dpp_tx_lock);
    if (!exact) return ESP_ERR_INVALID_STATE;
    if (queued) return ESP_OK;
    if (esp32qjs_dpp_async_register(0, 10000, esp32qjs_dpp_tx_poll,
        (void *)(uintptr_t)(ticket >> 32), (void *)(uintptr_t)(uint32_t)ticket) == 0) return ESP_OK;
    portENTER_CRITICAL(&s_dpp_tx_lock);
    if (s_dpp_tx.ticket == ticket) {
        s_dpp_tx.poll_queued = false;
        if (s_dpp_tx.error == ESP_OK) s_dpp_tx.error = ESP_ERR_NO_MEM;
    }
    portEXIT_CRITICAL(&s_dpp_tx_lock);
    return ESP_ERR_NO_MEM;
}

/* Native request/dwell and actual output EB must both retire. Logical TX
 * completion, native record memset and recycler entry are not sufficient. */
static esp_err_t esp32qjs_dpp_tx_reap_locked(uint64_t ticket, esp_err_t *outcome)
{
    *outcome = ESP_OK;
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    struct esp32qjs_dpp_tx snapshot = esp32qjs_dpp_tx_snapshot();
    if (!snapshot.held) return ESP_OK;
    if (snapshot.ticket != ticket || !snapshot.returned) return ESP_ERR_NOT_FINISHED;
    esp_err_t error = esp32qjs_wifi_offchan_tx_poll_native(ticket);
    if (error != ESP_OK) return error;
    error = esp32_mquickjs_wifi_action_sdk_quiescent();
    if (error != ESP_OK) return error;
    esp32qjs_wifi_offchan_tx_status_t buffer;
    error = esp32qjs_wifi_offchan_tx_status(ticket, &buffer);
    if (error != ESP_OK) return error;
    if (buffer.buffer_present || buffer.recycling) return ESP_ERR_NOT_FINISHED;
    /* Recycling can finish between the poll's delivery check and this call.
     * Keep a captured but undelivered result for the next native poll. */
    if (snapshot.completed && !snapshot.delivered && !snapshot.cancel_requested) return ESP_ERR_NOT_FINISHED;
    error = esp32qjs_wifi_offchan_tx_release(ticket);
    if (error != ESP_OK) return error;
    if (s_dpp_result) esp32qjs_dpp_tx_buffer_status(&s_dpp_result->status, &buffer);
    *outcome = snapshot.error;
    if (*outcome == ESP_OK) *outcome = buffer.error;
    if (!snapshot.cancel_requested && !snapshot.completed && *outcome == ESP_OK)
        *outcome = ESP_ERR_INVALID_STATE;
    esp32qjs_dpp_async_cancel(esp32qjs_dpp_tx_poll,
        (void *)(uintptr_t)(ticket >> 32), (void *)(uintptr_t)(uint32_t)ticket);
    portENTER_CRITICAL(&s_dpp_tx_lock);
    s_dpp_tx.held = false;
    s_dpp_tx.poll_queued = false;
    s_dpp_tx.error = *outcome;
    portEXIT_CRITICAL(&s_dpp_tx_lock);
    s_dpp_tx_submitted = false;
    s_dpp_tx_cancelled = false;
    s_dpp_ctx.pending_tx_op_in_progress = s_dpp_tx_deferred != NULL;
    return ESP_OK;
}

static esp_err_t esp32qjs_dpp_tx_cancel_current_locked(void)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    struct esp32qjs_dpp_tx snapshot = esp32qjs_dpp_tx_snapshot();
    if (!snapshot.held) return ESP_OK;
    if (!snapshot.returned) return ESP_ERR_NOT_FINISHED;
    portENTER_CRITICAL(&s_dpp_tx_lock);
    s_dpp_tx.cancel_requested = true;
    s_dpp_tx.delivered = true; /* No protocol timeout/retry after cancellation. */
    portEXIT_CRITICAL(&s_dpp_tx_lock);
    esp_err_t outcome;
    if (esp32qjs_dpp_tx_reap_locked(snapshot.ticket, &outcome) == ESP_OK) return ESP_OK;
    esp_err_t error = ESP_OK;
    if (!s_dpp_tx_cancelled) {
        /* The dwell may have ended while an EB is still in HMAC/PP. Do not
         * issue a cancellation against an already empty native operation. */
        esp_err_t native = esp32_mquickjs_wifi_action_sdk_quiescent();
        if (native == ESP_OK) s_dpp_tx_cancelled = true;
        else if (native != ESP_ERR_TIMEOUT) return native;
    }
    if (!s_dpp_tx_cancelled) {
        wifi_action_tx_req_t request = {.ifx = WIFI_IF_STA, .type = WIFI_OFFCHAN_TX_CANCEL,
            .rx_cb = s_action_rx_cb, .op_id = snapshot.op_id, .channel = snapshot.channel,
            .sec_channel = WIFI_SECOND_CHAN_NONE};
        error = esp_wifi_action_tx_req(&request);
        if (s_dpp_result) s_dpp_result->status.tx_cancel_error = error;
        if (error == ESP_OK) s_dpp_tx_cancelled = true;
    }
    esp_err_t retired = esp32qjs_dpp_tx_reap_locked(snapshot.ticket, &outcome);
    return retired == ESP_OK ? ESP_OK : error != ESP_OK ? error : retired;
}

static esp_err_t esp32qjs_dpp_tx_submit_native_locked(wifi_action_tx_req_t *request,
    enum dpp_tx_frame_type type, uint64_t ticket)
{
    if (esp32qjs_dpp_roc_held()) return ESP_ERR_NOT_FINISHED;
    esp_err_t error = esp32_mquickjs_wifi_action_sdk_quiescent();
    if (error != ESP_OK) return error;
    error = esp32qjs_wifi_offchan_tx_reserve(ticket, (uint32_t)(uintptr_t)s_action_rx_cb, request->channel);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL(&s_dpp_tx_lock);
    s_dpp_tx = (struct esp32qjs_dpp_tx){.ticket = ticket, .generation = s_dpp_generation,
        .type = type, .channel = request->channel, .held = true};
    portEXIT_CRITICAL(&s_dpp_tx_lock);
    error = esp32qjs_dpp_tx_schedule_locked(ticket);
    if (error != ESP_OK) {
        esp32qjs_wifi_offchan_tx_release(ticket); /* no driver admission yet */
        portENTER_CRITICAL(&s_dpp_tx_lock);
        s_dpp_tx.held = false;
        portEXIT_CRITICAL(&s_dpp_tx_lock);
        return error;
    }
    s_dpp_tx_submitted = true;
    s_dpp_tx_cancelled = false;
    error = esp_wifi_action_tx_req(request);
    s_dpp_result->status.tx_submit_error = error;
    portENTER_CRITICAL(&s_dpp_tx_lock);
    s_dpp_tx.op_id = request->op_id;
    s_dpp_tx.returned = true;
    if (error != ESP_OK) s_dpp_tx.error = error;
    else if (s_dpp_tx.captured && s_dpp_tx.captured_id != request->op_id)
        s_dpp_tx.error = ESP_ERR_INVALID_STATE;
    error = s_dpp_tx.error;
    portEXIT_CRITICAL(&s_dpp_tx_lock);
    s_dpp_ctx.pending_tx_op.op_id = request->op_id;
    s_dpp_ctx.pending_tx_op.type = type;
    s_dpp_ctx.pending_tx_op_in_progress = error == ESP_OK;
    return error;
}

static void esp32qjs_dpp_tx_deferred_discard_locked(void)
{
    struct esp32qjs_dpp_tx_deferred *pending = s_dpp_tx_deferred;
    if (!pending) return;
    s_dpp_tx_deferred = NULL;
    esp32qjs_dpp_async_cancel(esp32qjs_dpp_tx_deferred_poll,
        (void *)(uintptr_t)(pending->ticket >> 32), (void *)(uintptr_t)(uint32_t)pending->ticket);
    bin_clear_free(pending, sizeof(*pending) + pending->request.data_len);
}

static esp_err_t esp32qjs_dpp_tx_cancel_locked(void)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp32qjs_dpp_tx_deferred_discard_locked();
    s_dpp_ctx.pending_tx_op_in_progress = false;
    return esp32qjs_dpp_tx_cancel_current_locked();
}

static int esp32qjs_dpp_tx_deferred_schedule(uint64_t ticket)
{
    return esp32qjs_dpp_async_register(0, 10000, esp32qjs_dpp_tx_deferred_poll,
        (void *)(uintptr_t)(ticket >> 32), (void *)(uintptr_t)(uint32_t)ticket);
}

static esp_err_t esp32qjs_dpp_tx_submit_locked(wifi_action_tx_req_t *request, enum dpp_tx_frame_type type)
{
    if (!current_task_is_wifi_task() || !s_dpp_result || s_dpp_result->status.closing ||
        s_dpp_result->status.terminal || !atomic_load(&s_dpp_init_done) ||
        atomic_load(&dpp_shutting_down)) return ESP_ERR_INVALID_STATE;
    if (!request || !request->data_len || request->data_len > ESP32QJS_DPP_TX_MAX ||
        request->ifx != WIFI_IF_STA || request->rx_cb != s_action_rx_cb || !request->channel ||
        request->type != WIFI_OFFCHAN_TX_REQ || request->sec_channel != WIFI_SECOND_CHAN_NONE)
        return ESP_ERR_INVALID_ARG;
    if (s_dpp_tx_deferred) return ESP_ERR_NOT_FINISHED;
    uint64_t ticket = 0;
    esp_err_t error = esp32qjs_wifi_offchan_tx_allocate_ticket(&ticket);
    if (error != ESP_OK) return error;
    error = esp32qjs_dpp_tx_cancel_current_locked();
    if (error == ESP_OK) return esp32qjs_dpp_tx_submit_native_locked(request, type, ticket);
    if (error != ESP_ERR_NOT_FINISHED && error != ESP_ERR_TIMEOUT) return error;
    /* A valid RX response can precede the previous TX-done/recycle message.
     * Accept one bounded copied frame, then admit it natively only after the
     * previous request AND buffer have retired. No old op_id is reused. */
    struct esp32qjs_dpp_tx_deferred *pending = os_zalloc(sizeof(*pending) + request->data_len);
    if (!pending) return ESP_ERR_NO_MEM;
    pending->ticket = ticket; pending->generation = s_dpp_generation; pending->type = type;
    os_memcpy(&pending->request, request, sizeof(*request) + request->data_len);
    s_dpp_tx_deferred = pending;
    if (esp32qjs_dpp_tx_deferred_schedule(ticket) != 0) {
        esp32qjs_dpp_tx_deferred_discard_locked();
        return ESP_ERR_NO_MEM;
    }
    s_dpp_ctx.pending_tx_op_in_progress = true;
    return ESP_OK;
}

static void esp32qjs_dpp_tx_deferred_poll(void *high, void *low)
{
    uint64_t ticket = ((uint64_t)(uintptr_t)high << 32) | (uint32_t)(uintptr_t)low;
    if (!current_task_is_wifi_task() || dpp_api_lock() != ESP_OK) return;
    struct esp32qjs_dpp_tx_deferred *pending = s_dpp_tx_deferred;
    if (!pending || pending->ticket != ticket) { dpp_api_unlock(); return; }
    if (pending->generation != s_dpp_generation || !s_dpp_result || s_dpp_result->status.closing ||
        s_dpp_result->status.terminal || atomic_load(&dpp_shutting_down) || !atomic_load(&s_dpp_init_done)) {
        esp32qjs_dpp_tx_deferred_discard_locked(); dpp_api_unlock(); return;
    }
    esp_err_t error = esp32qjs_wifi_chm_timer_service_native();
    if (error == ESP_OK) error = esp32qjs_dpp_tx_cancel_current_locked();
    if (error == ESP_ERR_NOT_FINISHED || error == ESP_ERR_TIMEOUT) {
        if (esp32qjs_dpp_tx_deferred_schedule(ticket) == 0) { dpp_api_unlock(); return; }
        error = ESP_ERR_NO_MEM;
    }
    if (error == ESP_OK) {
        /* Detach before a synchronous native result can initiate cleanup. */
        s_dpp_tx_deferred = NULL;
        error = esp32qjs_dpp_tx_submit_native_locked(&pending->request, pending->type, ticket);
        bin_clear_free(pending, sizeof(*pending) + pending->request.data_len);
    } else esp32qjs_dpp_tx_deferred_discard_locked();
    if (error != ESP_OK) { dpp_abort_failure_locked(error); return; }
    dpp_api_unlock();
}

static void esp32qjs_dpp_tx_poll(void *high, void *low)
{
    uint64_t ticket = ((uint64_t)(uintptr_t)high << 32) | (uint32_t)(uintptr_t)low;
    if (!current_task_is_wifi_task() || dpp_api_lock() != ESP_OK) return;
    struct esp32qjs_dpp_tx snapshot = esp32qjs_dpp_tx_snapshot();
    if (!snapshot.held || snapshot.ticket != ticket || snapshot.generation != s_dpp_generation ||
        atomic_load(&dpp_shutting_down)) { dpp_api_unlock(); return; }
    esp_err_t timer_error = esp32qjs_wifi_chm_timer_service_native();
    if (timer_error != ESP_OK) { dpp_abort_failure_locked(timer_error); return; }
    snapshot = esp32qjs_dpp_tx_snapshot();
    if (!snapshot.held || snapshot.ticket != ticket) { dpp_api_unlock(); return; }
    esp_err_t buffer_error = esp32qjs_wifi_offchan_tx_poll_native(ticket);
    esp32qjs_wifi_offchan_tx_status_t buffer = {0};
    if (buffer_error == ESP_OK) buffer_error = esp32qjs_wifi_offchan_tx_status(ticket, &buffer);
    if (buffer_error == ESP_OK) buffer_error = buffer.error;
    if (buffer_error != ESP_OK) { dpp_abort_failure_locked(buffer_error); return; }
    portENTER_CRITICAL(&s_dpp_tx_lock);
    s_dpp_tx.poll_queued = false;
    bool deliver = snapshot.returned && snapshot.completed && !snapshot.delivered &&
        !snapshot.cancel_requested && snapshot.error == ESP_OK && !buffer.buffer_present && !buffer.recycling && s_dpp_result &&
        !s_dpp_result->status.closing && !s_dpp_result->status.terminal;
    if (deliver) s_dpp_tx.delivered = true;
    portEXIT_CRITICAL(&s_dpp_tx_lock);
    if (deliver) {
        s_dpp_ctx.pending_tx_op_in_progress = false;
        esp32qjs_dpp_tx_protocol_result(snapshot.type, snapshot.status);
    }
    /* Protocol processing can stop/cancel this operation. Do not reap a
     * successor admitted by a nested native command. */
    snapshot = esp32qjs_dpp_tx_snapshot();
    if (!snapshot.held || snapshot.ticket != ticket) { dpp_api_unlock(); return; }
    if (snapshot.error != ESP_OK) {
        dpp_abort_failure_locked(snapshot.error);
        return;
    }
    esp_err_t outcome;
    esp_err_t error = esp32qjs_dpp_tx_reap_locked(ticket, &outcome);
    if (error == ESP_ERR_TIMEOUT || error == ESP_ERR_NOT_FINISHED)
        error = esp32qjs_dpp_tx_schedule_locked(ticket);
    if (error != ESP_OK || outcome != ESP_OK) {
        dpp_abort_failure_locked(error != ESP_OK ? error : outcome);
        return;
    }
    dpp_api_unlock();
}
