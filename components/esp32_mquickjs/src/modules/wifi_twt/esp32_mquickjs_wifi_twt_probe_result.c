#include "esp32_mquickjs_wifi_twt_probe_result.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "esp32_mquickjs_wifi_twt_setup_result.h"
#include "esp32_mquickjs_wifi_twt_information.h"
#include "esp32_mquickjs_wifi_twt_broadcast_event.h"
#include <stddef.h>
#include <string.h>

/* The reviewed C5 wifi_event_post waits forever through OSI _event_post.
 * Probe success/timeout/failure callers still have native cleanup AFTER that
 * call (timer deletion, active flag, PM wake release). Intercept at the native
 * source, retain a value result, and make this observation best effort. */
int __real_wifi_event_post(int event_id, void *data, size_t size);
ESP_EVENT_DECLARE_BASE(ESP32QJS_WIFI_RADIO_CONTROL_EVENT);
static DRAM_ATTR esp32_mquickjs_wifi_twt_probe_result_snapshot_t s_probe_result;
static DRAM_ATTR portMUX_TYPE s_probe_result_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(WIFI_EVENT_ITWT_PROBE == 30 && sizeof(wifi_event_sta_itwt_probe_t) == 8 &&
    offsetof(wifi_event_sta_itwt_probe_t, reason) == 4 && ITWT_PROBE_SUCCESS == 1 &&
    ITWT_PROBE_STA_DISCONNECTED == 3, "reviewed C5 probe event ABI");

esp_err_t esp32_mquickjs_wifi_twt_probe_result_begin_native(uint32_t *identity)
{
    if (identity == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    esp_err_t error = s_probe_result.fault;
    if (error == ESP_OK && (s_probe_result.owned || s_probe_result.submitting || s_probe_result.observation_calls != 0U ||
        (s_probe_result.cancel_requested && !s_probe_result.cancel_complete)))
        error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && s_probe_result.identity == UINT32_MAX)
        error = s_probe_result.fault = ESP_ERR_NO_MEM;
    if (error == ESP_OK) {
        uint32_t next = s_probe_result.identity + 1U;
        s_probe_result = (esp32_mquickjs_wifi_twt_probe_result_snapshot_t){
            .identity = next, .submitting = true,
        };
        *identity = next;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_probe_result_submitted_native(uint32_t identity, esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    if (identity != 0U && s_probe_result.identity == identity && s_probe_result.submitting) {
        s_probe_result.submit_error = error;
        s_probe_result.submitted = error == ESP_OK;
        s_probe_result.submitting = false;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
}
void esp32_mquickjs_wifi_twt_probe_result_failed_native(esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    if (error != ESP_OK && s_probe_result.identity != 0U && s_probe_result.control_error == ESP_OK &&
        (s_probe_result.submitting || s_probe_result.submitted)) s_probe_result.control_error = error;
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
}
esp_err_t esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(uint32_t identity)
{
    if (identity == 0U) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    esp_err_t error = ESP_OK;
    if (identity != s_probe_result.identity || s_probe_result.submitting || s_probe_result.observation_calls != 0U)
        error = ESP_ERR_INVALID_STATE;
    else s_probe_result.cancel_requested = true;
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(uint32_t identity, esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    if (identity != 0U && identity == s_probe_result.identity && s_probe_result.cancel_requested) {
        s_probe_result.cancel_error = error;
        s_probe_result.cancel_complete = error == ESP_OK;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
}
bool esp32_mquickjs_wifi_twt_probe_result_claim_native(uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    bool exact = identity != 0U && identity == s_probe_result.identity && !s_probe_result.submitting && !s_probe_result.owned;
    if (exact) s_probe_result.owned = true;
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
    return exact;
}
bool esp32_mquickjs_wifi_twt_probe_result_release_native(uint32_t identity, uint32_t sequence)
{
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    bool exact = identity != 0U && identity == s_probe_result.identity && s_probe_result.owned &&
        s_probe_result.cancel_complete && !s_probe_result.submitting && s_probe_result.observation_calls == 0U &&
        sequence != 0U && sequence == s_probe_result.fence_sequence && s_probe_result.event_fenced;
    if (exact) s_probe_result.owned = false;
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
    return exact;
}
esp_err_t esp32_mquickjs_wifi_twt_probe_result_post_fence(uint32_t identity, uint32_t *sequence)
{
    if (identity == 0U || sequence == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    esp_err_t error = ESP_OK;
    if (identity != s_probe_result.identity || !s_probe_result.owned || !s_probe_result.cancel_complete ||
        s_probe_result.submitting || s_probe_result.observation_calls != 0U || s_probe_result.fence_pending)
        error = ESP_ERR_INVALID_STATE;
    else if (s_probe_result.fence_sequence == UINT32_MAX) error = ESP_ERR_NO_MEM;
    esp32_mquickjs_wifi_twt_probe_event_fence_t event = {.identity = identity};
    if (error == ESP_OK) {
        event.sequence = ++s_probe_result.fence_sequence;
        s_probe_result.fence_pending = true;
        s_probe_result.event_fenced = false;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
    if (error != ESP_OK) return error;
    error = esp_event_post(ESP32QJS_WIFI_RADIO_CONTROL_EVENT, ESP32_MQUICKJS_WIFI_TWT_PROBE_FENCE_EVENT,
        &event, sizeof(event), 0);
    if (error == ESP_OK) *sequence = event.sequence;
    else {
        portENTER_CRITICAL_SAFE(&s_probe_result_lock);
        if (s_probe_result.identity == identity && s_probe_result.fence_sequence == event.sequence)
            s_probe_result.fence_pending = false;
        portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
    }
    return error;
}
void esp32_mquickjs_wifi_twt_probe_result_observe_fence(const esp32_mquickjs_wifi_twt_probe_event_fence_t *event)
{
    if (event == NULL) return;
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    if (event->identity != 0U && event->identity == s_probe_result.identity && s_probe_result.owned &&
        event->sequence == s_probe_result.fence_sequence && s_probe_result.fence_pending) {
        s_probe_result.event_fenced = true;
        s_probe_result.fence_pending = false;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
}
int __wrap_wifi_event_post(int event_id, void *data, size_t size)
{
    if (event_id == WIFI_EVENT_BTWT_SETUP || event_id == WIFI_EVENT_BTWT_TEARDOWN)
        return esp32_mquickjs_wifi_btwt_event_post(event_id, data, size);
    if (event_id == WIFI_EVENT_ITWT_SUSPEND) return esp32_mquickjs_wifi_twt_information_post(data, size);
    if (event_id == WIFI_EVENT_ITWT_TEARDOWN) return esp32_mquickjs_wifi_twt_setup_result_teardown_post(data, size);
    if (event_id == WIFI_EVENT_ITWT_SETUP) return esp32_mquickjs_wifi_twt_setup_result_post(data, size);
    if (event_id != WIFI_EVENT_ITWT_PROBE) return __real_wifi_event_post(event_id, data, size);
    wifi_event_sta_itwt_probe_t event;
    memset(&event, 0, sizeof(event));
    bool valid = data != NULL && size == sizeof(event);
    if (valid) {
        /* Copy defined fields only, never SDK stack padding. Success does not
         * define a failure reason. Preserve the raw byte for other statuses. */
        memcpy(&event.status, data, sizeof(event.status));
        valid = (unsigned)event.status <= ITWT_PROBE_STA_DISCONNECTED;
        if (valid && event.status != ITWT_PROBE_SUCCESS)
            memcpy(&event.reason, (const uint8_t *)data + offsetof(wifi_event_sta_itwt_probe_t, reason), 1);
    }
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    if (!valid || s_probe_result.observation_calls == UINT32_MAX) {
        esp_err_t error = valid ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
        if (s_probe_result.fault == ESP_OK) s_probe_result.fault = error;
        portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
        return error;
    }
    ++s_probe_result.observation_calls;
    uint32_t identity = s_probe_result.identity;
    bool captured = identity != 0U && !s_probe_result.event_seen && !s_probe_result.cancel_requested &&
        (s_probe_result.submitting || s_probe_result.submitted);
    if (captured) {
        s_probe_result.event = event;
        s_probe_result.event_seen = true;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
    /* No SDK/event/allocator call under our lock. Queue full/OOM returns to
     * native cleanup; it cannot erase the retained result or block a close. */
    esp_err_t error = esp_event_post(WIFI_EVENT, event_id, &event, sizeof(event), 0);
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    --s_probe_result.observation_calls;
    if (captured && s_probe_result.identity == identity) s_probe_result.observation_error = error;
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_probe_result_snapshot(esp32_mquickjs_wifi_twt_probe_result_snapshot_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL_SAFE(&s_probe_result_lock);
    *out = s_probe_result;
    portEXIT_CRITICAL_SAFE(&s_probe_result_lock);
}
#endif
