#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

/* A wireless-enabled build reserves the common Future service once, including
 * its non-wireless calls. This is shared capacity, not per-module duplication. */
static inline const char *esp32_mquickjs_memory_shared_runtime_owner(void)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW || CONFIG_ESP32_MQUICKJS_FEATURE_BLE
    return "wireless.runtime";
#else
    return NULL;
#endif
}

/* These factories own the exact static RTOS control/storage allocation.
 * A non-NULL owner has boot lifetime and selects wireless admission. Internal
 * completion/control queues and synchronization objects may use control reserve;
 * observation/data queues use queue quota.
 * Delete only after producers, waiters and queued callbacks have retired. */
QueueHandle_t esp32_mquickjs_memory_queue_create(const char *owner,
    size_t capacity, size_t item_size, bool control);
void esp32_mquickjs_memory_queue_delete(QueueHandle_t queue);
SemaphoreHandle_t esp32_mquickjs_memory_mutex_create(const char *owner);
void esp32_mquickjs_memory_mutex_delete(SemaphoreHandle_t mutex);
EventGroupHandle_t esp32_mquickjs_memory_event_group_create(const char *owner);
void esp32_mquickjs_memory_event_group_delete(EventGroupHandle_t group);
