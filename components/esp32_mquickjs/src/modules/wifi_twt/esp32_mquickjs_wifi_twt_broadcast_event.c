#include "esp32_mquickjs_wifi_twt_broadcast_event.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static DRAM_ATTR esp32_mquickjs_wifi_btwt_event_scope_t *s_btwt_event_scope;
static DRAM_ATTR TaskHandle_t s_btwt_event_task;
static DRAM_ATTR portMUX_TYPE s_btwt_event_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(WIFI_EVENT_BTWT_SETUP == 33 && WIFI_EVENT_BTWT_TEARDOWN == 34 &&
    sizeof(wifi_event_sta_btwt_setup_t) == 32 && offsetof(wifi_event_sta_btwt_setup_t, btwt_id) == 8 &&
    offsetof(wifi_event_sta_btwt_setup_t, reason) == 16 && offsetof(wifi_event_sta_btwt_setup_t, target_wake_time) == 24 &&
    sizeof(wifi_event_sta_btwt_teardown_t) == 8 && offsetof(wifi_event_sta_btwt_teardown_t, status) == 4,
    "reviewed C5 broadcast event ABI");

static bool btwt_event_normalize(int event_id, const void *data, size_t size,
    esp32_mquickjs_wifi_btwt_event_scope_t *out)
{
    *out = (esp32_mquickjs_wifi_btwt_event_scope_t){.event_id = event_id};
    if (data == NULL) return false;
    if (event_id == WIFI_EVENT_BTWT_SETUP) {
        if (size != sizeof(wifi_event_sta_btwt_setup_t)) return false;
        wifi_event_sta_btwt_setup_t *event = &out->event.setup;
        memcpy(&event->status, data, sizeof(event->status));
        memcpy(&event->btwt_id, (const uint8_t *)data + 8, 1);
        if ((unsigned)event->status > BTWT_SETUP_INTERNAL_ERR || event->btwt_id >= 32) return false;
        out->broadcast_id = event->btwt_id;
        if (event->status == BTWT_SETUP_TXFAIL) memcpy(&event->reason, (const uint8_t *)data + 16, 1);
        if (event->status == BTWT_SETUP_SUCCESS || event->status == BTWT_SETUP_INTERNAL_ERR) {
            memcpy(&event->setup_cmd, (const uint8_t *)data + 4, sizeof(event->setup_cmd));
            if ((unsigned)event->setup_cmd > TWT_REJECT) return false;
        }
        if (event->status == BTWT_SETUP_SUCCESS) {
            uint8_t trigger;
            memcpy(&event->min_wake_dura, (const uint8_t *)data + 9, 1);
            memcpy(&event->wake_invl_expn, (const uint8_t *)data + 10, 1);
            memcpy(&event->wake_invl_mant, (const uint8_t *)data + 12, 2);
            memcpy(&trigger, (const uint8_t *)data + 14, 1);
            memcpy(&event->flow_type, (const uint8_t *)data + 15, 1);
            memcpy(&event->target_wake_time, (const uint8_t *)data + 24, 8);
            if (trigger > 1 || event->flow_type > 1 || event->wake_invl_expn > 31 ||
                event->setup_cmd != TWT_ACCEPT || !event->min_wake_dura || !event->wake_invl_mant) return false;
            event->trigger = trigger != 0;
        }
        return true;
    }
    if (event_id == WIFI_EVENT_BTWT_TEARDOWN) {
        if (size != sizeof(wifi_event_sta_btwt_teardown_t)) return false;
        wifi_event_sta_btwt_teardown_t *event = &out->event.teardown;
        memcpy(&event->btwt_id, data, 1);
        memcpy(&event->status, (const uint8_t *)data + 4, sizeof(event->status));
        if (event->btwt_id > 32 || (unsigned)event->status > BTWT_TEARDOWN_SUCCESS) return false;
        out->broadcast_id = event->btwt_id;
        return true;
    }
    return false;
}
bool esp32_mquickjs_wifi_btwt_event_begin(esp32_mquickjs_wifi_btwt_event_scope_t *scope, int event_id, uint8_t broadcast_id)
{
    if (scope == NULL || broadcast_id > 32 ||
        (event_id != WIFI_EVENT_BTWT_SETUP && event_id != WIFI_EVENT_BTWT_TEARDOWN) ||
        (event_id == WIFI_EVENT_BTWT_SETUP && broadcast_id == 32)) return false;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_btwt_event_lock);
    bool valid = task != NULL && (s_btwt_event_scope == NULL || s_btwt_event_task == task);
    /* A caller cannot push the same stack address twice. */
    for (const esp32_mquickjs_wifi_btwt_event_scope_t *p = s_btwt_event_scope; valid && p != NULL; p = p->previous)
        if (p == scope) valid = false;
    if (valid) {
        *scope = (esp32_mquickjs_wifi_btwt_event_scope_t){.previous = s_btwt_event_scope,
            .event_id = event_id, .broadcast_id = broadcast_id};
        s_btwt_event_scope = scope; s_btwt_event_task = task;
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_event_lock);
    return valid;
}
bool esp32_mquickjs_wifi_btwt_event_end(esp32_mquickjs_wifi_btwt_event_scope_t *scope)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_btwt_event_lock);
    bool exact = scope != NULL && s_btwt_event_scope == scope && s_btwt_event_task == task;
    if (exact) {
        s_btwt_event_scope = scope->previous; scope->previous = NULL;
        if (s_btwt_event_scope == NULL) s_btwt_event_task = NULL;
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_event_lock);
    return exact;
}
esp_err_t esp32_mquickjs_wifi_btwt_event_publish(const esp32_mquickjs_wifi_btwt_event_scope_t *scope)
{
    if (scope == NULL || !scope->seen) return ESP_OK;
    if (scope->event_id != WIFI_EVENT_BTWT_SETUP && scope->event_id != WIFI_EVENT_BTWT_TEARDOWN)
        return ESP_ERR_INVALID_ARG;
    size_t size = scope->event_id == WIFI_EVENT_BTWT_SETUP ? sizeof(scope->event.setup) : sizeof(scope->event.teardown);
    return esp_event_post(WIFI_EVENT, scope->event_id, &scope->event, size, 0);
}
esp_err_t esp32_mquickjs_wifi_btwt_event_post(int event_id, const void *data, size_t size)
{
    esp32_mquickjs_wifi_btwt_event_scope_t value;
    bool valid = btwt_event_normalize(event_id, data, size, &value);
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    bool captured = false;
    esp_err_t error = valid ? ESP_OK : ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_btwt_event_lock);
    esp32_mquickjs_wifi_btwt_event_scope_t *scope = s_btwt_event_scope;
    if (scope != NULL && s_btwt_event_task == task && scope->event_id == event_id) {
        if (!valid) {
            if (scope->error == ESP_OK) scope->error = ESP_ERR_INVALID_ARG;
        } else if (scope->broadcast_id == value.broadcast_id) {
            captured = true;
            if (!scope->seen) { scope->event = value.event; scope->seen = true; }
            else if (memcmp(&scope->event, &value.event, sizeof(value.event)) != 0) {
                scope->ambiguous = true;
                if (scope->error == ESP_OK) scope->error = ESP_ERR_INVALID_STATE;
            }
            error = scope->error;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_event_lock);
    if (!valid || captured) return error;
    /* Foreign/unscoped observations carry no invented owner identity. They
     * also use zero wait, so a full observer queue cannot hold native cleanup. */
    value.seen = true;
    return esp32_mquickjs_wifi_btwt_event_publish(&value);
}
#endif
