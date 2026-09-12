#include "esp32_mquickjs_wifi_twt_setup_result.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

ESP_EVENT_DECLARE_BASE(ESP32QJS_WIFI_RADIO_CONTROL_EVENT);
#define SETUP_FENCE_FLAGS (ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_PENDING | \
    ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_POSTED | ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_SEEN)

static DRAM_ATTR struct {
    esp32_mquickjs_wifi_twt_setup_result_t *entries;
    esp32_mquickjs_wifi_twt_setup_results_snapshot_t snapshot;
} s_setup_results = {.snapshot.last_request_id = -1};
static DRAM_ATTR portMUX_TYPE s_setup_results_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(WIFI_EVENT_ITWT_SETUP == 28 && sizeof(wifi_event_sta_itwt_setup_t) == 32 &&
    offsetof(wifi_event_sta_itwt_setup_t, status) == 16 && offsetof(wifi_event_sta_itwt_setup_t, reason) == 20 &&
    offsetof(wifi_event_sta_itwt_setup_t, target_wake_time) == 24 &&
    sizeof(esp32_mquickjs_wifi_twt_setup_result_t) == 72, "reviewed C5 setup result ABI");

static esp32_mquickjs_wifi_twt_setup_result_t *setup_result_find_locked(uint32_t identity)
{
    if (identity != 0U && s_setup_results.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i)
            if (s_setup_results.entries[i].identity == identity) return &s_setup_results.entries[i];
    return NULL;
}
static void setup_result_changed_locked(esp32_mquickjs_wifi_twt_setup_result_t *entry)
{
    if (entry->revision != UINT32_MAX) ++entry->revision;
    else {
        entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS;
        if (s_setup_results.snapshot.fault == ESP_OK) s_setup_results.snapshot.fault = ESP_ERR_NO_MEM;
    }
}
static bool setup_result_allocate(void)
{
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    bool present = s_setup_results.entries != NULL;
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    if (present) return true;
    esp32_mquickjs_wifi_twt_setup_result_t *fresh = esp32_mquickjs_memory_wireless_calloc("wifi", 8, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (fresh == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    if (s_setup_results.entries == NULL) {
        s_setup_results.entries = fresh;
        s_setup_results.snapshot.reserved_bytes = 8U * sizeof(*fresh);
        fresh = NULL;
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    if (fresh != NULL) esp32_mquickjs_memory_payload_free(fresh);
    return true;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_begin_native(int16_t request_id, uint32_t *identity)
{
    if (request_id < 0 || identity == NULL || *identity != 0U) return ESP_ERR_INVALID_ARG;
    if (!setup_result_allocate()) return ESP_ERR_NO_MEM;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp_err_t error = s_setup_results.snapshot.fault;
    if (error == ESP_OK && request_id <= s_setup_results.snapshot.last_request_id) error = ESP_ERR_INVALID_ARG;
    if (error == ESP_OK && s_setup_results.snapshot.last_identity == UINT32_MAX)
        error = s_setup_results.snapshot.fault = ESP_ERR_NO_MEM;
    esp32_mquickjs_wifi_twt_setup_result_t *entry = NULL;
    if (error == ESP_OK) {
        for (unsigned i = 0; i < 8U; ++i)
            if (s_setup_results.entries[i].identity == 0U) { entry = &s_setup_results.entries[i]; break; }
        if (entry == NULL) error = ESP_ERR_WIFI_TWT_FULL;
    }
    if (error == ESP_OK) {
        *entry = (esp32_mquickjs_wifi_twt_setup_result_t){
            .identity = ++s_setup_results.snapshot.last_identity, .revision = 1,
            .request_id = request_id, .flags = ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING};
        s_setup_results.snapshot.last_request_id = request_id;
        *identity = entry->identity;
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_setup_result_submitted_native(uint32_t identity, esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
    if (entry != NULL && (entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING)) {
        entry->submit_error = error;
        entry->flags &= (uint16_t)~ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING;
        if (error == ESP_OK) entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTED;
        setup_result_changed_locked(entry);
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
}
bool esp32_mquickjs_wifi_twt_setup_result_cancelled_native(uint32_t identity, uint8_t flows)
{
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
    bool valid = entry != NULL && !(entry->flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING));
    if (valid) {
        entry->cancelled_flows |= flows;
        entry->flags = (entry->flags & (uint16_t)~SETUP_FENCE_FLAGS) | ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED;
        setup_result_changed_locked(entry);
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return valid;
}
void esp32_mquickjs_wifi_twt_setup_results_connection_closed_native(void)
{
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    if (s_setup_results.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            esp32_mquickjs_wifi_twt_setup_result_t *entry = &s_setup_results.entries[i];
            if (entry->identity == 0U || (entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED)) continue;
            /* Do not drop SUBMITTING, overwrite the original result/error,
             * fabricate CANCELLED, or release any owner. Late observations may
             * revoke the new fence again but never restore this authority. */
            entry->flags = (entry->flags & (uint16_t)~SETUP_FENCE_FLAGS) | ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED;
            setup_result_changed_locked(entry);
        }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
}
bool esp32_mquickjs_wifi_twt_setup_request_closed_native(int16_t request_id)
{
    bool closed = false;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    if (request_id >= 0 && s_setup_results.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            const esp32_mquickjs_wifi_twt_setup_result_t *entry = &s_setup_results.entries[i];
            if (entry->identity != 0U && entry->request_id == request_id) {
                closed = (entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED) != 0U;
                break;
            }
        }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return closed;
}
void esp32_mquickjs_wifi_twt_setup_result_request_close(uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
    if (entry != NULL && !(entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_CLOSE_REQUESTED)) {
        entry->flags = (entry->flags & (uint16_t)~SETUP_FENCE_FLAGS) | ESP32_MQUICKJS_WIFI_TWT_SETUP_CLOSE_REQUESTED;
        setup_result_changed_locked(entry);
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
}
bool esp32_mquickjs_wifi_twt_setup_request_closing(int16_t request_id)
{
    bool closing = false;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    if (request_id >= 0 && s_setup_results.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            const esp32_mquickjs_wifi_twt_setup_result_t *entry = &s_setup_results.entries[i];
            if (entry->identity != 0U && entry->request_id == request_id) {
                closing = (entry->flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_CLOSE_REQUESTED |
                    ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED)) != 0U;
                break;
            }
        }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return closing;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_read(uint32_t identity,
    esp32_mquickjs_wifi_twt_setup_result_t *out)
{
    if (identity == 0U || out == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
    esp_err_t error = entry == NULL ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (entry != NULL) *out = *entry;
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(uint32_t identity, uint8_t flow)
{
    if (identity == 0U || flow > 7U) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    bool conflict = false;
    if (s_setup_results.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            const esp32_mquickjs_wifi_twt_setup_result_t *other = &s_setup_results.entries[i];
            if (other->identity != 0U && other->identity != identity &&
                (other->flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED) && other->teardown_flow == flow)
                conflict = true;
        }
    if (!conflict && entry != NULL && entry->observation_calls == 0U &&
        (entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN) && entry->event.status == 1 &&
        entry->event.config.flow_id == flow &&
        !(entry->flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED |
            ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS | ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED |
            ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED))) {
        if (entry->revision == UINT32_MAX) error = ESP_ERR_NO_MEM;
        else {
            entry->teardown_flow = flow;
            entry->flags = (entry->flags & (uint16_t)~SETUP_FENCE_FLAGS) |
                ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED | ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING;
            setup_result_changed_locked(entry);
            error = ESP_OK;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(uint32_t identity, esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
    if (entry != NULL && (entry->flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING)) {
        entry->teardown_error = error;
        entry->flags &= (uint16_t)~ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING;
        setup_result_changed_locked(entry);
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_teardown_post(const void *data, size_t size)
{
    _Static_assert(WIFI_EVENT_ITWT_TEARDOWN == 29 && sizeof(wifi_event_sta_itwt_teardown_t) == 8 &&
        offsetof(wifi_event_sta_itwt_teardown_t, status) == 4, "reviewed teardown event ABI");
    if (data == NULL || size != sizeof(wifi_event_sta_itwt_teardown_t)) return ESP_ERR_INVALID_ARG;
    wifi_event_sta_itwt_teardown_t input, event = {0};
    memcpy(&input, data, sizeof(input));
    if (input.flow_id > 8U || (input.status != ITWT_TEARDOWN_SUCCESS && input.status != ITWT_TEARDOWN_FAIL))
        return ESP_ERR_INVALID_ARG;
    event.flow_id = input.flow_id;
    event.status = input.status;
    uint32_t identities[8];
    unsigned count = 0;
    esp32_mquickjs_wifi_twt_setup_result_t *first = NULL;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    if (s_setup_results.entries != NULL) {
        for (unsigned i = 0; i < 8U; ++i) {
            esp32_mquickjs_wifi_twt_setup_result_t *entry = &s_setup_results.entries[i];
            if (entry->identity == 0U || !(entry->flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED) ||
                entry->teardown_flow != event.flow_id) continue;
            if (first != NULL) {
                first->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS;
                entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS;
            } else first = entry;
            entry->flags &= (uint16_t)~(SETUP_FENCE_FLAGS | ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED);
            if (!(entry->flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN)) {
                entry->teardown_status = (uint8_t)event.status;
                entry->flags |= ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN;
            } else if (entry->teardown_status != (uint8_t)event.status)
                entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS;
            setup_result_changed_locked(entry);
            /* A flow-only event is not sufficient to choose between records.
             * Preserve ambiguity for every candidate, never pick a new ID. */
            if (entry->observation_calls == UINT32_MAX) {
                entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS;
                if (s_setup_results.snapshot.fault == ESP_OK) s_setup_results.snapshot.fault = ESP_ERR_INVALID_STATE;
                continue;
            }
            ++entry->observation_calls;
            identities[count++] = entry->identity;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    esp_err_t error = esp_event_post(WIFI_EVENT, WIFI_EVENT_ITWT_TEARDOWN, &event, sizeof(event), 0);
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    for (unsigned i = 0; i < count; ++i) {
        esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identities[i]);
        if (entry == NULL) continue;
        --entry->observation_calls;
        if (entry->teardown_observation_error == ESP_OK && error != ESP_OK) entry->teardown_observation_error = error;
        setup_result_changed_locked(entry);
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return error;
}
bool esp32_mquickjs_wifi_twt_setup_result_release_native(uint32_t identity, uint32_t revision,
    uint32_t sequence)
{
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
    bool exact = entry != NULL && revision != 0U && revision != UINT32_MAX && entry->revision == revision &&
        sequence != 0U && entry->fence_sequence == sequence &&
        (entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED) &&
        (entry->flags & SETUP_FENCE_FLAGS) ==
            (ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_POSTED | ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_SEEN) &&
        !(entry->flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING)) && entry->observation_calls == 0U;
    if (exact) memset(entry, 0, sizeof(*entry));
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return exact;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_post_fence(uint32_t identity, uint32_t revision,
    uint32_t *sequence)
{
    if (identity == 0U || revision == 0U || sequence == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_setup_event_fence_t event = {.identity = identity};
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
    esp_err_t error = ESP_OK;
    if (entry == NULL || entry->revision != revision || entry->observation_calls != 0U ||
        !(entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED) ||
        (entry->flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_PENDING)))
        error = ESP_ERR_INVALID_STATE;
    else if (entry->revision == UINT32_MAX || entry->fence_sequence == UINT32_MAX) error = ESP_ERR_NO_MEM;
    else {
        event.sequence = ++entry->fence_sequence;
        entry->flags = (entry->flags & (uint16_t)~SETUP_FENCE_FLAGS) | ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_PENDING;
        ++entry->observation_calls; /* Pin through post, including an early callback. */
        setup_result_changed_locked(entry);
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    if (error != ESP_OK) return error;
    error = esp_event_post(ESP32QJS_WIFI_RADIO_CONTROL_EVENT, ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_EVENT,
        &event, sizeof(event), 0);
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    entry = setup_result_find_locked(identity);
    /* No release or second fence can pass while this post pins the entry. */
    if (entry != NULL && entry->fence_sequence == event.sequence) {
        --entry->observation_calls;
        bool valid = (entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_PENDING) != 0U;
        if (error == ESP_OK && valid) {
            entry->flags &= (uint16_t)~ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_PENDING;
            entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_POSTED;
            *sequence = event.sequence;
        } else {
            entry->flags &= (uint16_t)~SETUP_FENCE_FLAGS;
            if (error == ESP_OK) error = ESP_ERR_NOT_FINISHED;
        }
        setup_result_changed_locked(entry);
    } else if (error == ESP_OK) error = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_setup_result_observe_fence(const esp32_mquickjs_wifi_twt_setup_event_fence_t *event)
{
    if (event == NULL || event->sequence == 0U) return;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(event->identity);
    if (entry != NULL && entry->fence_sequence == event->sequence &&
        (entry->flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_PENDING | ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_POSTED)) &&
        !(entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_SEEN)) {
        entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_SEEN;
        setup_result_changed_locked(entry);
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
}
static void setup_result_normalize(wifi_event_sta_itwt_setup_t *out, const wifi_event_sta_itwt_setup_t *input)
{
    memset(out, 0, sizeof(*out));
#define COPY(field) out->field = input->field
    COPY(config.setup_cmd); COPY(config.trigger); COPY(config.flow_type); COPY(config.flow_id);
    COPY(config.wake_invl_expn); COPY(config.wake_duration_unit); COPY(config.reserved);
    COPY(config.min_wake_dura); COPY(config.wake_invl_mant); COPY(config.twt_id); COPY(config.timeout_time_ms);
    COPY(status); COPY(target_wake_time);
    if (input->status != 1) COPY(reason); /* Success has no TX failure reason. */
#undef COPY
}
static bool setup_result_same(const wifi_event_sta_itwt_setup_t *a, const wifi_event_sta_itwt_setup_t *b)
{
#define SAME(field) (a->field == b->field)
    return SAME(config.setup_cmd) && SAME(config.trigger) && SAME(config.flow_type) && SAME(config.flow_id) &&
        SAME(config.wake_invl_expn) && SAME(config.wake_duration_unit) && SAME(config.reserved) &&
        SAME(config.min_wake_dura) && SAME(config.wake_invl_mant) && SAME(config.twt_id) && SAME(config.timeout_time_ms) &&
        SAME(status) && SAME(reason) && SAME(target_wake_time);
#undef SAME
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_post(const void *data, size_t size)
{
    if (data == NULL || size != sizeof(wifi_event_sta_itwt_setup_t)) {
        portENTER_CRITICAL_SAFE(&s_setup_results_lock);
        if (s_setup_results.snapshot.fault == ESP_OK) s_setup_results.snapshot.fault = ESP_ERR_INVALID_ARG;
        portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
        return ESP_ERR_INVALID_ARG;
    }
    wifi_event_sta_itwt_setup_t input, event;
    memcpy(&input, data, sizeof(input));
    setup_result_normalize(&event, &input);
    uint32_t identity = 0;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    if (s_setup_results.entries != NULL) {
        for (unsigned i = 0; i < 8U; ++i) {
            esp32_mquickjs_wifi_twt_setup_result_t *entry = &s_setup_results.entries[i];
            if (entry->identity == 0U || event.config.twt_id != entry->request_id) continue;
            entry->flags &= (uint16_t)~(SETUP_FENCE_FLAGS | ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED);
            if (!(entry->flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN)) {
                entry->event = event;
                entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN;
            } else if (!setup_result_same(&entry->event, &event)) entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS;
            setup_result_changed_locked(entry);
            if (entry->observation_calls == UINT32_MAX) {
                entry->flags |= ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS;
                if (s_setup_results.snapshot.fault == ESP_OK) s_setup_results.snapshot.fault = ESP_ERR_INVALID_STATE;
                portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
                return ESP_ERR_INVALID_STATE;
            }
            identity = entry->identity;
            ++entry->observation_calls;
            break;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    /* The fixed SDK's callers clear pending/timer state after this returns.
     * Default-event-loop saturation must not block that native cleanup. */
    esp_err_t error = esp_event_post(WIFI_EVENT, WIFI_EVENT_ITWT_SETUP, &event, sizeof(event), 0);
    if (identity != 0U) {
        portENTER_CRITICAL_SAFE(&s_setup_results_lock);
        esp32_mquickjs_wifi_twt_setup_result_t *entry = setup_result_find_locked(identity);
        if (entry != NULL) {
            --entry->observation_calls;
            if (entry->observation_error == ESP_OK && error != ESP_OK) entry->observation_error = error;
            setup_result_changed_locked(entry);
        }
        portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
    }
    return error;
}
void esp32_mquickjs_wifi_twt_setup_results_snapshot(esp32_mquickjs_wifi_twt_setup_results_snapshot_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL_SAFE(&s_setup_results_lock);
    *out = s_setup_results.snapshot;
    if (s_setup_results.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i)
            if (s_setup_results.entries[i].identity != 0U) out->owned_mask |= (uint8_t)(1U << i);
    portEXIT_CRITICAL_SAFE(&s_setup_results_lock);
}
#endif
