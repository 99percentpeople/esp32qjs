#include "esp32_mquickjs_wifi_twt_teardown_tx.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32_mquickjs_wifi_twt_tx.h"
#include "esp32_mquickjs_wifi_twt_broadcast_timer.h"
#include "esp32_mquickjs_wifi_twt_broadcast_event.h"
#include "esp32_mquickjs_wifi_twt_sdk.h"
#include "esp_wifi.h"
#include <stddef.h>
#include <string.h>

static DRAM_ATTR struct {
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t state;
    void *buffer;
    uintptr_t node;
    TaskHandle_t task;
    unsigned passthrough_depth;
} s_teardown_tx;
static DRAM_ATTR portMUX_TYPE s_teardown_tx_lock = portMUX_INITIALIZER_UNLOCKED;
void pm_twt_wake_up(void);
void pm_twt_wake_done(void);
void __real_he_twt_teardown_txcb(void *buffer);

static void IRAM_ATTR teardown_tx_changed_locked(void)
{
    if (s_teardown_tx.state.revision != UINT32_MAX) ++s_teardown_tx.state.revision;
    else if (s_teardown_tx.state.fault == ESP_OK) s_teardown_tx.state.fault = ESP_ERR_NO_MEM;
}
static bool teardown_tx_task(void)
{
    return !xPortInIsrContext() && s_teardown_tx.task == xTaskGetCurrentTaskHandle();
}
/* Native task only. Revoke our authority before calling shared PM. */
static void teardown_tx_wake_done(void)
{
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool release = s_teardown_tx.state.wake_held && !s_teardown_tx.state.acquiring && !s_teardown_tx.state.releasing;
    if (release) {
        s_teardown_tx.state.wake_held = false;
        s_teardown_tx.state.releasing = true;
        teardown_tx_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    if (!release) return;
    pm_twt_wake_done();
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    s_teardown_tx.state.releasing = false;
    teardown_tx_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
}
void esp32_mquickjs_wifi_twt_teardown_wake_up_native(void)
{
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool managed = s_teardown_tx.state.submitting && !s_teardown_tx.state.callback_busy &&
        !s_teardown_tx.state.recycling && !s_teardown_tx.state.recycled &&
        !s_teardown_tx.passthrough_depth && teardown_tx_task();
    bool acquire = managed && !s_teardown_tx.state.wake_held && !s_teardown_tx.state.acquiring &&
        !s_teardown_tx.state.releasing && !s_teardown_tx.state.output_seen && s_teardown_tx.state.fault == ESP_OK;
    if (acquire) {
        s_teardown_tx.state.wake_held = true;
        s_teardown_tx.state.acquiring = true;
        teardown_tx_changed_locked();
    } else if (managed && s_teardown_tx.state.fault == ESP_OK) {
        s_teardown_tx.state.fault = ESP_ERR_INVALID_STATE;
        teardown_tx_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    if (!managed) { pm_twt_wake_up(); return; }
    if (!acquire) return;
    pm_twt_wake_up();
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    s_teardown_tx.state.acquiring = false;
    teardown_tx_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
}
void esp32_mquickjs_wifi_twt_teardown_wake_done_native(void)
{
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool managed = s_teardown_tx.state.callback_busy && !s_teardown_tx.passthrough_depth && teardown_tx_task();
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    if (managed) teardown_tx_wake_done();
    else pm_twt_wake_done();
}
static esp_err_t teardown_tx_begin(uint32_t identity, uintptr_t node, uint8_t flow, bool broadcast)
{
    if (identity == 0U || node == 0U || flow > (broadcast ? 31U : 7U) || xPortInIsrContext()) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    esp_err_t error = s_teardown_tx.state.identity != 0U ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (error == ESP_OK) {
        s_teardown_tx.state = (esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t){
            .identity = identity, .revision = 1, .flow = flow, .broadcast = broadcast, .submitting = true};
        s_teardown_tx.node = node;
        s_teardown_tx.task = task;
    }
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_begin_native(uint32_t identity, uintptr_t node, uint8_t flow)
{
    return teardown_tx_begin(identity, node, flow, false);
}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_begin_broadcast_native(uint32_t identity, uintptr_t node, uint8_t slot)
{
    return teardown_tx_begin(identity, node, slot, true);
}
bool esp32_mquickjs_wifi_twt_teardown_tx_abandon_native(uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool exact = identity != 0U && s_teardown_tx.state.identity == identity && teardown_tx_task() &&
        s_teardown_tx.state.submitting && !s_teardown_tx.state.output_seen &&
        !s_teardown_tx.state.wake_held && !s_teardown_tx.state.acquiring && !s_teardown_tx.state.releasing;
    if (exact) memset(&s_teardown_tx, 0, sizeof(s_teardown_tx));
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    return exact;
}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_end_native(uint32_t identity, esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool exact = identity != 0U && s_teardown_tx.state.identity == identity && teardown_tx_task() && s_teardown_tx.state.submitting;
    bool no_output = exact && !s_teardown_tx.state.output_seen;
    if (exact) {
        s_teardown_tx.state.submitting = false;
        s_teardown_tx.state.submit_error = error;
        teardown_tx_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    if (!exact) return ESP_ERR_INVALID_STATE;
    if (no_output) teardown_tx_wake_done(); /* e.g. management allocation failed */
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    esp_err_t fault = s_teardown_tx.state.fault;
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    return fault;
}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_output_native(void *node, void *buffer, bool tracked, uint32_t *identity)
{
    if (identity == NULL) return ESP_ERR_INVALID_ARG;
    *identity = 0;
    uint8_t broadcast_slot = 0;
    bool broadcast_frame = esp32_mquickjs_wifi_twt_tx_broadcast_teardown_fields(buffer, false, &broadcast_slot);
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool managed = s_teardown_tx.state.submitting && !s_teardown_tx.state.callback_busy &&
        !s_teardown_tx.state.recycling && !s_teardown_tx.state.recycled &&
        !s_teardown_tx.passthrough_depth && teardown_tx_task();
    esp_err_t error = ESP_OK;
    if (managed) {
        const uint8_t *metadata = NULL;
        if (buffer != NULL) memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
        bool valid = tracked && metadata != NULL && (uintptr_t)node == s_teardown_tx.node &&
            (s_teardown_tx.state.broadcast ?
                (broadcast_frame && broadcast_slot == s_teardown_tx.state.flow &&
                    ((const uint8_t *)buffer)[44] == 0U && ((const uint8_t *)buffer)[62] == 1U) :
                (((const uint8_t *)buffer)[44] == 1U && ((const uint8_t *)buffer)[62] == 0U &&
                    metadata[57] == s_teardown_tx.state.flow)) && !s_teardown_tx.state.output_seen &&
            s_teardown_tx.state.wake_held && !s_teardown_tx.state.acquiring && s_teardown_tx.state.fault == ESP_OK;
        if (valid) {
            s_teardown_tx.buffer = buffer;
            s_teardown_tx.state.output_seen = true;
            *identity = s_teardown_tx.state.identity;
        } else {
            error = tracked ? ESP_ERR_INVALID_STATE : ESP_ERR_NO_MEM;
            if (s_teardown_tx.state.fault == ESP_OK) s_teardown_tx.state.fault = error;
        }
        teardown_tx_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_teardown_tx_output_returned(void *buffer, uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    if (identity != 0U && s_teardown_tx.state.identity == identity && buffer != NULL &&
        s_teardown_tx.buffer == buffer && s_teardown_tx.state.output_seen) {
        s_teardown_tx.state.output_returned = true;
        teardown_tx_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
}
void IRAM_ATTR esp32_mquickjs_wifi_twt_teardown_tx_recycle(void *buffer, uint32_t identity, bool entering)
{
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    if (identity != 0U && s_teardown_tx.state.identity == identity && buffer != NULL &&
        s_teardown_tx.buffer == buffer && s_teardown_tx.state.output_seen && !s_teardown_tx.state.recycled) {
        s_teardown_tx.state.recycling = entering;
        if (!entering) s_teardown_tx.state.recycled = true;
        teardown_tx_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
}
static void teardown_tx_broadcast_complete(void *buffer, uint32_t identity, uintptr_t node, uint8_t slot)
{
    const uint8_t *metadata;
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
    uintptr_t current = 0;
    uint8_t parsed = 0;
    bool exact = metadata != NULL && ((const uint8_t *)buffer)[44] == 0U &&
        ((const uint8_t *)buffer)[62] == 1U &&
        esp32_mquickjs_wifi_twt_tx_broadcast_teardown_fields(buffer, true, &parsed) && parsed == slot &&
        esp32_mquickjs_wifi_btwt_setup_owner_native(slot, identity, &current) && current == node;
    esp32_mquickjs_wifi_btwt_event_scope_t scope = {0};
    bool entered = exact && esp32_mquickjs_wifi_btwt_event_begin(&scope, WIFI_EVENT_BTWT_TEARDOWN, slot);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (entered) {
        esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_complete_native(node, slot, metadata[19]);
        bool ended = esp32_mquickjs_wifi_btwt_event_end(&scope);
        error = !ended || !scope.seen || scope.ambiguous ? ESP_ERR_INVALID_STATE : scope.error;
    }
    /* Save control before zero-wait observation. callback_busy retains this
     * singleton through publication; no native buffer is read after callback. */
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    s_teardown_tx.state.completion_error = error;
    s_teardown_tx.state.completion_seen = scope.seen;
    s_teardown_tx.state.completion_ambiguous = scope.ambiguous;
    if (scope.seen) s_teardown_tx.state.completion_status = scope.event.teardown.status;
    teardown_tx_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    esp_err_t observed = entered ? esp32_mquickjs_wifi_btwt_event_publish(&scope) : ESP_OK;
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    s_teardown_tx.state.observation_error = observed;
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
}
void __wrap_he_twt_teardown_txcb(void *buffer)
{
    if (buffer == NULL) return;
    uint32_t current_identity = 0;
    bool known = esp32_mquickjs_wifi_twt_tx_teardown_identity(buffer, &current_identity);
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool managed = s_teardown_tx.state.identity != 0U && s_teardown_tx.buffer == buffer &&
        (!known || current_identity == s_teardown_tx.state.identity);
    bool accept = managed && known && current_identity == s_teardown_tx.state.identity &&
        teardown_tx_task() && !s_teardown_tx.state.callback_seen &&
        !s_teardown_tx.state.callback_busy && !s_teardown_tx.state.recycling && !s_teardown_tx.state.recycled;
    uint32_t identity = s_teardown_tx.state.identity;
    uintptr_t node = s_teardown_tx.node;
    uint8_t flow = s_teardown_tx.state.flow;
    bool broadcast = s_teardown_tx.state.broadcast;
    if (accept) {
        s_teardown_tx.state.callback_seen = true;
        s_teardown_tx.state.callback_busy = true;
        teardown_tx_changed_locked();
    }
    bool passthrough = !managed && s_teardown_tx.state.callback_busy && teardown_tx_task();
    if (passthrough) ++s_teardown_tx.passthrough_depth;
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    if (!managed) {
        __real_he_twt_teardown_txcb(buffer);
        if (passthrough) {
            portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
            --s_teardown_tx.passthrough_depth;
            portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
        }
        return;
    }
    if (!accept) return;
    const uint8_t *metadata;
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
    if (broadcast) teardown_tx_broadcast_complete(buffer, identity, node, flow);
    else if (metadata != NULL && metadata[57] == flow && ((const uint8_t *)buffer)[44] == 1U &&
        ((const uint8_t *)buffer)[62] == 0U &&
        esp32_mquickjs_wifi_twt_sdk_teardown_tx_matches_native(identity, node, flow))
        __real_he_twt_teardown_txcb(buffer);
    /* The original callback may recycle; do not inspect the EB after return.
     * A skipped stale callback still owes only this operation's wake ref. */
    teardown_tx_wake_done();
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    s_teardown_tx.state.callback_busy = false;
    teardown_tx_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
}
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(uint32_t identity, uint32_t *revision)
{
    if (identity == 0U || revision == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool exact = s_teardown_tx.state.identity == identity && teardown_tx_task();
    bool quiet = exact && !s_teardown_tx.state.submitting && !s_teardown_tx.state.callback_busy &&
        !s_teardown_tx.state.acquiring && !s_teardown_tx.state.releasing && !s_teardown_tx.state.recycling &&
        (!s_teardown_tx.state.output_seen || (s_teardown_tx.state.output_returned && s_teardown_tx.state.recycled));
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    if (!exact) return ESP_ERR_INVALID_STATE;
    if (!quiet) return ESP_ERR_NOT_FINISHED;
    teardown_tx_wake_done();
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    esp_err_t error = s_teardown_tx.state.fault;
    if (error == ESP_OK && s_teardown_tx.state.revision == UINT32_MAX) error = ESP_ERR_NO_MEM;
    if (error == ESP_OK) *revision = s_teardown_tx.state.revision;
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    return error;
}
bool esp32_mquickjs_wifi_twt_teardown_tx_release_native(uint32_t identity, uint32_t revision)
{
    uint32_t current;
    if (esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(identity, &current) != ESP_OK || current != revision)
        return false;
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool exact = s_teardown_tx.state.identity == identity && s_teardown_tx.state.revision == revision;
    if (exact) memset(&s_teardown_tx, 0, sizeof(s_teardown_tx));
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    return exact;
}
bool esp32_mquickjs_wifi_twt_teardown_tx_holds_broadcast(unsigned slot, uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    bool held = identity != 0 && s_teardown_tx.state.broadcast &&
        s_teardown_tx.state.identity == identity && s_teardown_tx.state.flow == slot;
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
    return held;
}
void esp32_mquickjs_wifi_twt_teardown_tx_snapshot(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL_SAFE(&s_teardown_tx_lock);
    *out = s_teardown_tx.state;
    portEXIT_CRITICAL_SAFE(&s_teardown_tx_lock);
}
#endif
