#include "esp32_mquickjs_wifi_wait.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <stdint.h>

static portMUX_TYPE s_wait_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_wait_task;
static TickType_t s_wait_started, s_wait_ticks;

esp_err_t esp32_mquickjs_wifi_wait_begin(uint32_t timeout_ms)
{
    if (timeout_ms == 0 || timeout_ms > INT32_MAX) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    TickType_t started = xTaskGetTickCount();
    /* Reserve less than half a tick cycle for wrap-safe finite waits; never
     * narrow a converted interval into TickType_t or its infinite sentinel. */
    uint64_t ticks_wide = ((uint64_t)timeout_ms * configTICK_RATE_HZ + 999U) / 1000U;
    if (ticks_wide == 0 || ticks_wide > (uint64_t)((TickType_t)-1) / 2U) return ESP_ERR_INVALID_ARG;
    TickType_t ticks = (TickType_t)ticks_wide;
    portENTER_CRITICAL(&s_wait_lock);
    if (s_wait_task != NULL) {
        portEXIT_CRITICAL(&s_wait_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_wait_started = started;
    s_wait_ticks = ticks;
    s_wait_task = task;
    portEXIT_CRITICAL(&s_wait_lock);
    return ESP_OK;
}

TickType_t esp32_mquickjs_wifi_wait_remaining(TickType_t fallback)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    TickType_t now = xTaskGetTickCount();
    portENTER_CRITICAL(&s_wait_lock);
    TickType_t remaining = fallback;
    if (s_wait_task != NULL && s_wait_task == task) {
        TickType_t elapsed = (TickType_t)(now - s_wait_started);
        remaining = elapsed >= s_wait_ticks ? 0 : s_wait_ticks - elapsed;
    }
    portEXIT_CRITICAL(&s_wait_lock);
    return remaining;
}

void esp32_mquickjs_wifi_wait_end(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_wait_lock);
    if (s_wait_task == task) s_wait_task = NULL;
    portEXIT_CRITICAL(&s_wait_lock);
}
#endif
