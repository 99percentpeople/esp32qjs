/* Included in esp_nan_usd.c after the SDK channel conversion helpers. This
 * state is allocated from the shared wireless budget, once per USD engine.
 * Driver buffers remain in the common DPP/USD off-channel ledger. */
#include "esp32_mquickjs_wifi_offchan_frame.h"
#include "esp32_mquickjs_wifi_chm_timer.h"
#include "esp32_mquickjs_wifi_action_sdk.h"
#include "esp32_mquickjs_memory.h"
#include "esp_timer.h"

extern bool current_task_is_wifi_task(void);
int esp_nan_de_rx_action(uint8_t *header, uint8_t *payload, size_t length, uint8_t channel);
typedef struct {
    uint64_t ticket;
    uint32_t engine;
    esp_err_t error, cleanup_error;
    int64_t completed_us;
    int frequency;
    uint8_t op_id, seen_id, channel;
    bool held, roc, returned, captured, tx_done, tx_success, duration_done;
    bool cancel_requested, cancel_written, poll_queued;
    /* One accepted successor can await the old EB's physical recycler. */
    wifi_action_tx_req_t *pending;
    size_t pending_bytes;
    /* One public follow-up shares this transport; its result survives later
     * discovery frames until the parent worker consumes it. */
    esp32_mquickjs_wifi_nan_message_tx_status_t message;
    bool message_scope, pending_message;
    uint8_t service_scope, service, pending_service;
} esp32qjs_usd_transport_t;
static esp32qjs_usd_transport_t *s_esp32qjs_usd_transport;
static void esp32qjs_nan_usd_transport_poll(void *high, void *low);
static esp_err_t esp32qjs_nan_usd_transport_reap(bool cancel);

/* Called around native SDF construction on the Wi-Fi task with the USD mutex.
 * This records which exact service must drain before its raw ID can be reused. */
uint8_t esp32qjs_nan_usd_tx_service_scope(uint8_t service)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t) return 0;
    uint8_t previous = t->service_scope;
    t->service_scope = service;
    return previous;
}

bool esp32qjs_nan_usd_is_action_callback(uintptr_t callback)
{ return callback == (uintptr_t)esp_nan_de_rx_action; }

static esp_err_t esp32qjs_nan_usd_transport_open(void)
{
    if (s_esp32qjs_usd_transport) return ESP_ERR_INVALID_STATE;
    s_esp32qjs_usd_transport = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1,
        sizeof(*s_esp32qjs_usd_transport), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    return s_esp32qjs_usd_transport ? ESP_OK : ESP_ERR_NO_MEM;
}

static void esp32qjs_nan_usd_transport_snapshot(esp32_mquickjs_wifi_nan_usd_sdk_status_t *status)
{
    const esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t) return;
    status->operation = t->ticket;
    status->transport_held = t->held || t->pending;
    status->transport_error = t->error;
    status->cleanup_error = t->cleanup_error;
    status->reserved_bytes = sizeof(*t) + t->pending_bytes;
    status->tx_done = t->tx_done;
    esp32qjs_wifi_offchan_tx_status_t buffer;
    status->buffer_retired = !t->held || t->roc ||
        (esp32qjs_wifi_offchan_tx_status(t->ticket, &buffer) == ESP_OK && !buffer.buffer_present && !buffer.recycling);
}

void esp32qjs_nan_usd_tx_status_capture(const wifi_event_action_tx_status_t *event)
{
    if (!event || !current_task_is_wifi_task() || !s_nan_usd_data_lock ||
        !esp32qjs_nan_usd_is_action_callback(event->context)) return;
    NAN_USD_DATA_LOCK();
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (t && t->held && !t->roc && event->ifx == WIFI_IF_STA && event->channel == t->channel &&
        (!t->returned || t->op_id == event->op_id)) {
        if (t->captured && t->seen_id != event->op_id) t->error = ESP_ERR_INVALID_RESPONSE;
        else {
            t->captured = true; t->seen_id = event->op_id;
            if (event->status == WIFI_ACTION_TX_DONE || event->status == WIFI_ACTION_TX_FAILED) {
                if (!t->tx_done) {
                    t->tx_done = true; t->tx_success = event->status == WIFI_ACTION_TX_DONE;
                    t->completed_us = esp_timer_get_time();
                    if (t->message.identity && t->message.ticket == t->ticket) {
                        t->message.tx_done = true;
                        t->message.tx_succeeded = t->tx_success;
                        t->message.has_native_status = true;
                        t->message.native_status = event->status;
                        t->message.completed_us = t->completed_us;
                    }
                }
            } else if (event->status == WIFI_ACTION_TX_DURATION_COMPLETED || event->status == WIFI_ACTION_TX_OP_CANCELLED)
                t->duration_done = true;
            else t->error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    NAN_USD_DATA_UNLOCK();
}

static void esp32qjs_nan_usd_roc_done(uint32_t context, uint8_t operation, wifi_roc_done_status_t status)
{
    if (!current_task_is_wifi_task() || !s_nan_usd_data_lock || !esp32qjs_nan_usd_is_action_callback(context)) return;
    NAN_USD_DATA_LOCK();
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (t && t->held && t->roc && (!t->returned || t->op_id == operation)) {
        if (t->captured && t->seen_id != operation) t->error = ESP_ERR_INVALID_RESPONSE;
        else {
            t->captured = true; t->seen_id = operation; t->duration_done = true;
            if (status != WIFI_ROC_DONE && !t->cancel_requested) t->error = ESP_FAIL;
        }
    }
    NAN_USD_DATA_UNLOCK();
}

static esp_err_t esp32qjs_nan_usd_transport_schedule(void)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t || !t->held) return ESP_ERR_INVALID_STATE;
    if (t->poll_queued) return ESP_OK;
    t->poll_queued = true;
    if (eloop_register_timeout(0, 10000, esp32qjs_nan_usd_transport_poll,
        (void *)(uintptr_t)(t->ticket >> 32), (void *)(uintptr_t)(uint32_t)t->ticket) == 0) return ESP_OK;
    t->poll_queued = false;
    return ESP_ERR_NO_MEM;
}

static void esp32qjs_nan_usd_pending_clear(void)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t || !t->pending) return;
    if (t->pending_message) t->message.buffer_retired = true;
    t->pending_message = false;
    t->pending_service = 0;
    forced_memzero(t->pending, t->pending_bytes);
    esp32_mquickjs_memory_payload_free(t->pending);
    t->pending = NULL; t->pending_bytes = 0;
}

/* Native Wi-Fi task, USD mutex held. No completion callback invokes this:
 * clearing the native operation and returning from its producer must precede
 * physical retirement and any next engine operation. */
static esp_err_t esp32qjs_nan_usd_transport_reap(bool cancel)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t || !t->held) return ESP_OK;
    if (!current_task_is_wifi_task() || !t->returned) return ESP_ERR_NOT_FINISHED;
    if (cancel) t->cancel_requested = true;
    esp_err_t error = esp32qjs_wifi_chm_timer_service_native();
    if (error && !t->error) t->error = error;
    if (!t->roc) {
        error = esp32qjs_wifi_offchan_tx_poll_native(t->ticket);
        if (error && !t->error) t->error = error;
    }
    esp_err_t quiescent = esp32_mquickjs_wifi_action_sdk_quiescent();
    if (cancel && quiescent == ESP_ERR_TIMEOUT && !t->cancel_written) {
        if (t->roc) {
            wifi_roc_req_t request = {.ifx = WIFI_IF_STA, .type = WIFI_ROC_CANCEL,
                .channel = t->channel, .op_id = t->op_id, .rx_cb = esp_nan_de_rx_action,
                .sec_channel = WIFI_SECOND_CHAN_NONE};
            error = esp_wifi_remain_on_channel(&request);
        } else {
            wifi_action_tx_req_t request = {.ifx = WIFI_IF_STA, .type = WIFI_OFFCHAN_TX_CANCEL,
                .channel = t->channel, .op_id = t->op_id, .rx_cb = esp_nan_de_rx_action,
                .sec_channel = WIFI_SECOND_CHAN_NONE};
            error = esp_wifi_action_tx_req(&request);
        }
        t->cleanup_error = error;
        if (error == ESP_OK) t->cancel_written = true;
        else return error;
        quiescent = esp32_mquickjs_wifi_action_sdk_quiescent();
    }
    if (quiescent != ESP_OK) return quiescent;
    if (!t->roc) {
        esp32qjs_wifi_offchan_tx_status_t buffer;
        error = esp32qjs_wifi_offchan_tx_status(t->ticket, &buffer);
        if (error) return error;
        if (buffer.buffer_present || buffer.recycling) return ESP_ERR_NOT_FINISHED;
        if (buffer.error && !t->error) t->error = buffer.error;
        error = esp32qjs_wifi_offchan_tx_release(t->ticket);
        if (error) return error;
    }
    bool roc = t->roc;
    int frequency = t->frequency;
    bool deliver = !t->cancel_requested && !s_esp32qjs_usd_closing && g_nan_de;
    if (deliver && ((!roc && !t->tx_done) || (roc && !t->duration_done)) && !t->error)
        t->error = ESP_FAIL;
    eloop_cancel_timeout(esp32qjs_nan_usd_transport_poll,
        (void *)(uintptr_t)(t->ticket >> 32), (void *)(uintptr_t)(uint32_t)t->ticket);
    t->held = t->poll_queued = false;
    if (t->message.identity && t->message.ticket == t->ticket)
        t->message.buffer_retired = true;
    t->cleanup_error = ESP_OK;
    if (t->error && !t->cancel_requested) {
        esp32qjs_nan_usd_record_error(t->error); s_esp32qjs_usd_closing = true;
    }
    /* ROC preemption must clear the old listen frequency. A queued TX has
     * already installed its new wait flags, so do not clear those when the
     * preceding cancelled TX finally retires. */
    if (g_nan_de && !s_esp32qjs_usd_closing && (deliver || roc || !t->pending)) {
        if (roc) nan_de_listen_ended(g_nan_de, frequency);
        else { nan_de_tx_status(g_nan_de, frequency, NULL); nan_de_tx_wait_ended(g_nan_de); }
    }
    return ESP_OK;
}

static esp_err_t esp32qjs_nan_usd_transport_start(wifi_action_tx_req_t *tx, wifi_roc_req_t *roc, int frequency)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!current_task_is_wifi_task() || !t || t->held || s_esp32qjs_usd_closing || s_esp32qjs_usd_error)
        return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_action_sdk_quiescent();
    if (error) return error;
    uint64_t ticket = 0;
    error = esp32qjs_wifi_offchan_tx_allocate_ticket(&ticket);
    if (error) return error;
    uint8_t channel = tx ? tx->channel : roc->channel;
    if (tx) {
        error = esp32qjs_wifi_offchan_tx_reserve(ticket, (uint32_t)(uintptr_t)esp_nan_de_rx_action, channel);
        if (error) return error;
    }
    esp32_mquickjs_wifi_nan_message_tx_status_t message = t->message;
    bool message_scope = t->message_scope;
    uint8_t service_scope = t->service_scope;
    *t = (esp32qjs_usd_transport_t){.ticket = ticket, .engine = s_esp32qjs_usd_identity,
        .frequency = frequency, .channel = channel, .held = true, .roc = roc != NULL,
        .message = message, .message_scope = message_scope,
        .service_scope = service_scope, .service = tx ? service_scope : 0};
    if (tx && message_scope) {
        t->message.ticket = ticket;
        t->message.buffer_retired = false;
    }
    error = esp32qjs_nan_usd_transport_schedule();
    if (error) {
        if (tx) esp32qjs_wifi_offchan_tx_release(ticket);
        t->held = false;
        if (message_scope) t->message.buffer_retired = true;
        return error;
    }
    if (roc) roc->done_cb = esp32qjs_nan_usd_roc_done;
    error = tx ? esp_wifi_action_tx_req(tx) : esp_wifi_remain_on_channel(roc);
    t->op_id = tx ? tx->op_id : roc->op_id;
    t->returned = true;
    if (tx && message_scope) {
        esp32qjs_wifi_offchan_tx_status_t buffer;
        if (!esp32qjs_wifi_offchan_tx_status(ticket, &buffer))
            t->message.allocated = buffer.buffer_present || buffer.recycled;
    }
    if (error) t->error = error;
    else if (t->captured && t->seen_id != t->op_id) t->error = ESP_ERR_INVALID_RESPONSE;
    return t->error;
}

static esp_err_t esp32qjs_nan_usd_transport_tx(wifi_action_tx_req_t *request, int frequency)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t || !request || !request->data_len || request->data_len > ESP_WIFI_MAX_FUP_SSI_LEN + 512U ||
        t->pending || s_esp32qjs_usd_closing) return ESP_ERR_INVALID_STATE;
    /* A background SDF must not preempt a public follow-up before retirement. */
    if (!t->message_scope && t->message.identity && !t->message.buffer_retired)
        return ESP_ERR_NOT_FINISHED;
    esp_err_t error = esp32qjs_nan_usd_transport_reap(true);
    if (!error) return esp32qjs_nan_usd_transport_start(request, NULL, frequency);
    if (error != ESP_ERR_TIMEOUT && error != ESP_ERR_NOT_FINISHED) return error;
    size_t bytes = sizeof(*request) + request->data_len;
    t->pending = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, bytes,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!t->pending) return ESP_ERR_NO_MEM;
    memcpy(t->pending, request, bytes); t->pending_bytes = bytes;
    t->pending_message = t->message_scope;
    t->pending_service = t->service_scope;
    if (t->pending_message) t->message.buffer_retired = false;
    error = esp32qjs_nan_usd_transport_schedule();
    if (error) esp32qjs_nan_usd_pending_clear();
    return error;
}

static esp_err_t esp32qjs_nan_usd_transport_listen(wifi_roc_req_t *request, int frequency)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t || t->pending || s_esp32qjs_usd_closing) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32qjs_nan_usd_transport_reap(false);
    if (error) return error;
    return esp32qjs_nan_usd_transport_start(NULL, request, frequency);
}

static esp_err_t esp32qjs_nan_usd_transport_service(bool close)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t) return ESP_OK;
    if (close) esp32qjs_nan_usd_pending_clear();
    esp_err_t error = esp32qjs_nan_usd_transport_reap(close || t->pending != NULL);
    if (error) return error;
    if (!t->pending || close || s_esp32qjs_usd_closing) return ESP_OK;
    wifi_action_tx_req_t *request = t->pending;
    size_t bytes = t->pending_bytes;
    bool message_scope = t->pending_message;
    uint8_t service = t->pending_service;
    t->pending_message = false;
    t->pending_service = 0;
    t->pending = NULL; t->pending_bytes = 0;
    t->message_scope = message_scope;
    uint8_t previous = esp32qjs_nan_usd_tx_service_scope(service);
    error = esp32qjs_nan_usd_transport_start(request, NULL, esp_nan_chan_to_freq(request->channel));
    esp32qjs_nan_usd_tx_service_scope(previous);
    t->message_scope = false;
    if (error && message_scope && !t->held) t->message.buffer_retired = true;
    forced_memzero(request, bytes); esp32_mquickjs_memory_payload_free(request);
    return error;
}

static esp_err_t esp32qjs_nan_usd_service_drain(uint8_t id)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t) return ESP_OK;
    if (t->pending && t->pending_service == id) esp32qjs_nan_usd_pending_clear();
    if (t->held && !t->roc && t->service == id)
        return esp32qjs_nan_usd_transport_reap(true);
    return ESP_OK;
}

static void esp32qjs_nan_usd_transport_poll(void *high, void *low)
{
    uint64_t ticket = ((uint64_t)(uintptr_t)high << 32) | (uint32_t)(uintptr_t)low;
    if (!current_task_is_wifi_task() || !s_nan_usd_data_lock) return;
    NAN_USD_DATA_LOCK();
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t || !t->held || t->ticket != ticket || t->engine != s_esp32qjs_usd_identity) goto done;
    t->poll_queued = false;
    esp_err_t error = esp32qjs_nan_usd_transport_service(s_esp32qjs_usd_closing);
    if (error == ESP_ERR_TIMEOUT || error == ESP_ERR_NOT_FINISHED || !error)
        error = t->held ? esp32qjs_nan_usd_transport_schedule() : ESP_OK;
    if (error) { esp32qjs_nan_usd_record_error(error); s_esp32qjs_usd_closing = true; }
done:
    NAN_USD_DATA_UNLOCK();
}

static int esp32qjs_nan_usd_poll_command(void *identity, void *close)
{
    if (!s_nan_usd_data_lock) return ESP_OK;
    NAN_USD_DATA_LOCK();
    esp_err_t error = (uint32_t)(uintptr_t)identity == s_esp32qjs_usd_identity ?
        esp32qjs_nan_usd_transport_service(close != NULL) : ESP_ERR_INVALID_STATE;
    NAN_USD_DATA_UNLOCK();
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_usd_sdk_poll(bool close)
{
    esp32_mquickjs_wifi_nan_usd_sdk_status_t status;
    esp32_mquickjs_wifi_nan_usd_sdk_status(&status);
    if (!status.identity || !status.transport_held) return ESP_OK;
    void *identity = (void *)(uintptr_t)status.identity;
    if (current_task_is_wifi_task()) return esp32qjs_nan_usd_poll_command(identity, (void *)(uintptr_t)close);
    return eloop_register_timeout_blocking(esp32qjs_nan_usd_poll_command, identity, (void *)(uintptr_t)close);
}

static esp_err_t esp32qjs_nan_usd_transport_free(void)
{
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    if (!t) return ESP_OK;
    if (t->held || t->pending) return ESP_ERR_NOT_FINISHED;
    s_esp32qjs_usd_transport = NULL;
    forced_memzero(t, sizeof(*t)); esp32_mquickjs_memory_payload_free(t);
    return ESP_OK;
}

bool esp32_mquickjs_wifi_nan_usd_message_status(uint32_t identity,
    esp32_mquickjs_wifi_nan_message_tx_status_t *status)
{
    if (!identity || !status || !s_nan_usd_data_lock) return false;
    NAN_USD_DATA_LOCK();
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    bool exact = t && t->message.identity == identity;
    if (exact) *status = t->message;
    NAN_USD_DATA_UNLOCK();
    return exact;
}

esp_err_t esp32_mquickjs_wifi_nan_usd_message_release(uint32_t identity)
{
    if (!identity || !s_nan_usd_data_lock) return ESP_ERR_INVALID_STATE;
    NAN_USD_DATA_LOCK();
    esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
    bool exact = t && t->message.identity == identity;
    bool retired = exact && t->message.buffer_retired && !t->message_scope;
    if (retired) memset(&t->message, 0, sizeof(t->message));
    NAN_USD_DATA_UNLOCK();
    return retired ? ESP_OK : exact ? ESP_ERR_NOT_FINISHED : ESP_ERR_INVALID_STATE;
}
