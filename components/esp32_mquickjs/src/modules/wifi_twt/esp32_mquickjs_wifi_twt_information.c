#include "esp32_mquickjs_wifi_twt_information.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_tx.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

typedef struct {
    esp32_mquickjs_wifi_twt_information_result_t result;
    esp32_mquickjs_wifi_twt_information_identity_t native;
    wifi_event_sta_itwt_suspend_t event;
    TaskHandle_t task;
    bool callback_busy, publishing, published;
} twt_information_operation_t;
static struct { twt_information_operation_t *operation; uint32_t last_identity; } s_information;
static portMUX_TYPE s_information_operation_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t esp32_mquickjs_wifi_twt_information_begin_native(uint32_t setup_identity,
    const esp32_mquickjs_wifi_twt_information_identity_t *native, uint32_t duration_ms, bool resume, uint32_t *identity)
{
    if (!setup_identity || native == NULL || identity == NULL || !native->node ||
        native->flows != (1U << (native->control & 7U)) ||
        native->control != ((native->control & 7U) | ((duration_ms || resume) ? 0x60U : 0U)) ||
        (resume && duration_ms != 0U) ||
        duration_ms > ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS) return ESP_ERR_INVALID_ARG;
    /* This allocation is boot-retained, not a per-operation allocation. Only
     * native queue callers create/reset the record; value readers use lock. */
    if (s_information.operation == NULL) {
        twt_information_operation_t *fresh = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (fresh == NULL) return ESP_ERR_NO_MEM;
        portENTER_CRITICAL_SAFE(&s_information_operation_lock);
        s_information.operation = fresh;
        portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    }
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    esp_err_t error = op->result.identity ? ESP_ERR_INVALID_STATE :
        s_information.last_identity == UINT32_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    if (error == ESP_OK) {
        *op = (twt_information_operation_t){.native = *native, .task = xTaskGetCurrentTaskHandle(),
            .result = {.identity = ++s_information.last_identity, .setup_identity = setup_identity,
                .duration_ms = duration_ms, .flow = native->control & 7U, .resume = resume, .submitting = true}};
        *identity = op->result.identity;
    }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    return error;
}
bool esp32_mquickjs_wifi_twt_information_producer_allowed(void *node, uint32_t flow,
    uint32_t size, uint32_t all, uint32_t duration_ms)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    const twt_information_operation_t *op = s_information.operation;
    bool allowed = op == NULL || !op->result.identity || (op->task == task && op->result.submitting &&
        !op->result.tx_identity && op->native.node == (uintptr_t)node && flow == op->result.flow &&
        !all && size == ((duration_ms || op->result.resume) ? 3U : 0U) && duration_ms == op->result.duration_ms);
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    return allowed;
}
void esp32_mquickjs_wifi_twt_information_bind_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native, uint32_t tx_identity)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    if (op != NULL && op->result.identity && op->result.submitting && op->task == task &&
        !op->result.tx_identity && native->node == op->native.node && native->control == op->native.control &&
        native->flows == op->native.flows && !memcmp(native->request_ids, op->native.request_ids, sizeof(native->request_ids)))
        op->result.tx_identity = tx_identity;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
}
static void information_publish(void)
{
    wifi_event_sta_itwt_suspend_t event;
    uint32_t identity = 0;
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    if (op != NULL && op->result.identity && (op->result.complete || op->result.tx_complete) && op->result.event_seen &&
        !op->publishing && !op->published) {
        op->publishing = true; event = op->event; identity = op->result.identity;
    }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    if (!identity) return;
    esp_err_t error = esp_event_post(WIFI_EVENT, WIFI_EVENT_ITWT_SUSPEND, &event, sizeof(event), 0);
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    op->result.observation_error = error; op->publishing = false; op->published = true;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
}
void esp32_mquickjs_wifi_twt_information_submitted_native(uint32_t identity, esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    if (op != NULL && identity && op->result.identity == identity) {
        op->result.submit_error = error; op->result.submitting = false;
        if (!op->result.tx_identity) {
            op->result.complete = true;
            if (!error && !op->result.event_seen) op->result.native_error = ESP_ERR_INVALID_STATE;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    information_publish();
}
bool esp32_mquickjs_wifi_twt_information_callback_begin(uint32_t tx_identity)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    bool exact = op != NULL && op->result.identity && tx_identity && op->result.tx_identity == tx_identity &&
        op->task == task && !op->callback_busy && !op->result.complete;
    if (exact) op->callback_busy = true;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    return exact;
}
void esp32_mquickjs_wifi_twt_information_callback_end(uint32_t tx_identity, bool valid)
{
    esp32_mquickjs_wifi_twt_information_timer_snapshot_t timer;
    esp32_mquickjs_wifi_twt_information_timer_snapshot(&timer);
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    if (op != NULL && op->result.identity && op->result.tx_identity == tx_identity && op->callback_busy) {
        if (!valid || !op->result.event_seen) op->result.native_error = ESP_ERR_INVALID_STATE;
        else if (timer.fault != ESP_OK) op->result.native_error = timer.fault;
        else if (!op->result.native_error && !(op->event.flow_id_bitmap & (1U << op->result.flow)))
            op->result.native_error = ESP_ERR_INVALID_STATE;
        if (op->result.resume && !op->result.native_error && !op->result.timer_identity)
            op->result.native_error = ESP_ERR_INVALID_STATE;
        op->callback_busy = false; op->result.tx_complete = true;
        op->result.complete = !op->result.resume || op->result.native_error != ESP_OK || op->result.resume_complete;
    }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    information_publish();
}
static bool information_same_native(const esp32_mquickjs_wifi_twt_information_identity_t *a,
    const esp32_mquickjs_wifi_twt_information_identity_t *b)
{
    return a->node == b->node && a->flows == b->flows && a->control == b->control &&
        !memcmp(a->request_ids, b->request_ids, sizeof(a->request_ids));
}
void esp32_mquickjs_wifi_twt_information_timer_bound_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native, uint32_t timer_identity)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    if (op != NULL && op->result.identity && op->result.resume && op->callback_busy && op->task == task &&
        native != NULL && information_same_native(native, &op->native)) {
        if (!timer_identity || op->result.timer_identity) op->result.native_error = ESP_ERR_INVALID_STATE;
        else op->result.timer_identity = timer_identity;
    }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
}
void esp32_mquickjs_wifi_twt_information_timer_finished_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native, uint32_t timer_identity, esp_err_t error)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    if (op != NULL && op->result.identity && op->result.resume && timer_identity &&
        op->result.timer_identity == timer_identity && op->task == task && native != NULL &&
        information_same_native(native, &op->native)) {
        op->result.resume_complete = error == ESP_OK;
        if (error != ESP_OK) op->result.native_error = error;
        if (op->result.tx_complete) op->result.complete = true;
    }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
}
void esp32_mquickjs_wifi_twt_information_refresh_native(void)
{
    esp32_mquickjs_wifi_twt_information_identity_t native = {0};
    uint32_t identity = 0;
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    if (op != NULL && op->result.identity && op->result.resume && !op->result.submitting &&
        !op->callback_busy && !op->result.complete) { identity = op->result.identity; native = op->native; }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    if (!identity) return;
    esp32_mquickjs_wifi_twt_information_timer_snapshot_t timer;
    esp32_mquickjs_wifi_twt_information_timer_snapshot(&timer);
    esp_err_t error = timer.fault;
    if (!esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(&native)) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) return;
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    if (op->result.identity == identity && !op->result.complete) {
        op->result.native_error = error; op->result.complete = true;
    }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
}
esp_err_t esp32_mquickjs_wifi_twt_information_post(const void *data, size_t size)
{
    _Static_assert(WIFI_EVENT_ITWT_SUSPEND == 31 && sizeof(wifi_event_sta_itwt_suspend_t) == 40 &&
        offsetof(wifi_event_sta_itwt_suspend_t, actual_suspend_time_ms) == 8, "reviewed information observation ABI");
    if (data == NULL || size != sizeof(wifi_event_sta_itwt_suspend_t)) return ESP_ERR_INVALID_ARG;
    wifi_event_sta_itwt_suspend_t input, event = {0};
    memcpy(&input, data, sizeof(input));
    event.status = input.status; event.flow_id_bitmap = input.flow_id_bitmap;
    for (unsigned i = 0; i < 8U; ++i)
        if (event.flow_id_bitmap & (1U << i)) event.actual_suspend_time_ms[i] = input.actual_suspend_time_ms[i];
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    bool captured = op != NULL && op->result.identity && op->task == task &&
        (op->callback_busy || (op->result.submitting && !op->result.tx_identity));
    if (captured) {
        if (op->result.event_seen) op->result.native_error = ESP_ERR_INVALID_STATE;
        else {
            op->event = event; op->result.event_seen = true;
            /* A timer binding/completion failure may precede this event.
             * A successful TX observation cannot erase that earlier error. */
            if (op->result.native_error == ESP_OK) op->result.native_error = event.status;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    return captured ? ESP_OK : esp_event_post(WIFI_EVENT, WIFI_EVENT_ITWT_SUSPEND, &event, sizeof(event), 0);
}
bool esp32_mquickjs_wifi_twt_information_read(uint32_t identity, esp32_mquickjs_wifi_twt_information_result_t *out)
{
    if (!identity || out == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    bool exact = op != NULL && op->result.identity == identity;
    if (exact) *out = op->result;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    return exact;
}
bool esp32_mquickjs_wifi_twt_information_pending(void)
{
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    bool pending = s_information.operation != NULL && s_information.operation->result.identity != 0U;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    return pending;
}
bool esp32_mquickjs_wifi_twt_information_snapshot(esp32_mquickjs_wifi_twt_information_result_t *out)
{
    if (out == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    bool present = s_information.operation != NULL && s_information.operation->result.identity != 0U;
    if (present) *out = s_information.operation->result;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    return present;
}
void esp32_mquickjs_wifi_twt_information_abandon(uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    if (s_information.operation != NULL && identity && s_information.operation->result.identity == identity)
        s_information.operation->result.abandoned = true;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
}
esp_err_t esp32_mquickjs_wifi_twt_information_reap_native(uint32_t setup_identity)
{
    esp32_mquickjs_wifi_twt_information_refresh_native();
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    twt_information_operation_t *op = s_information.operation;
    if (op == NULL || !op->result.identity || (setup_identity && setup_identity != op->result.setup_identity)) {
        portEXIT_CRITICAL_SAFE(&s_information_operation_lock); return ESP_OK;
    }
    if (setup_identity) op->result.abandoned = true;
    bool ready = op->result.abandoned && !op->result.submitting && !op->callback_busy && !op->publishing;
    uint32_t tx_identity = op->result.tx_identity;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    if (!ready) return ESP_ERR_NOT_FINISHED;
    esp32_mquickjs_wifi_twt_tx_cleanup_native();
    esp_err_t error = esp32_mquickjs_wifi_twt_tx_information_quiescent_native(tx_identity);
    portENTER_CRITICAL_SAFE(&s_information_operation_lock);
    if (error == ESP_OK) *op = (twt_information_operation_t){0};
    else op->result.cleanup_error = error;
    portEXIT_CRITICAL_SAFE(&s_information_operation_lock);
    return error;
}
#endif
