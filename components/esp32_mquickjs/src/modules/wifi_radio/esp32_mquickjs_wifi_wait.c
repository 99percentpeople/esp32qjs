#include "esp32_mquickjs_wifi_wait.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <stdint.h>

static portMUX_TYPE s_wait_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_wait_task;
static TickType_t s_wait_started, s_wait_ticks;

esp_err_t esp32_mquickjs_wifi_wait_begin(uint32_t timeout_ms)
{
    if (timeout_ms == 0 || timeout_ms > 60000U) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    TickType_t started = xTaskGetTickCount();
    /* Round up and avoid pdMS_TO_TICKS multiplication overflow. The supported
     * ESP-IDF tick rates keep this interval well below one TickType_t wrap. */
    TickType_t ticks = (TickType_t)(((uint64_t)timeout_ms * configTICK_RATE_HZ + 999U) / 1000U);
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
