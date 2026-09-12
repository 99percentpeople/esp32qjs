#include "esp32_mquickjs_memory_rtos.h"
#include "esp32_mquickjs_memory.h"
#include "esp_heap_caps.h"
#include <stdint.h>

static void *memory_rtos_allocate(const char *owner, size_t size, bool control)
{
    return owner != NULL
        ? esp32_mquickjs_memory_wireless_calloc(owner, 1, size,
            ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, control
                ? ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL : ESP32_MQUICKJS_MEMORY_BUDGET_QUEUE)
        : heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

QueueHandle_t esp32_mquickjs_memory_queue_create(const char *owner,
    size_t capacity, size_t item_size, bool control)
{
    if (capacity == 0 || item_size == 0 || capacity > (UBaseType_t)-1 ||
        item_size > (UBaseType_t)-1 || capacity > (SIZE_MAX - sizeof(StaticQueue_t)) / item_size)
        return NULL;
    StaticQueue_t *storage = memory_rtos_allocate(owner,
        sizeof(*storage) + capacity * item_size, control);
    if (storage == NULL) return NULL;
    QueueHandle_t queue = xQueueCreateStatic((UBaseType_t)capacity, (UBaseType_t)item_size,
        (uint8_t *)(storage + 1), storage);
    if (queue == NULL) esp32_mquickjs_memory_payload_free(storage);
    return queue;
}

void esp32_mquickjs_memory_queue_delete(QueueHandle_t queue)
{
    if (queue == NULL) return;
    /* The pinned IDF static factories return the supplied control address. */
    vQueueDelete(queue);
    esp32_mquickjs_memory_payload_free(queue);
}

SemaphoreHandle_t esp32_mquickjs_memory_mutex_create(const char *owner)
{
    StaticSemaphore_t *storage = memory_rtos_allocate(owner, sizeof(*storage), true);
    if (storage == NULL) return NULL;
    SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(storage);
    if (mutex == NULL) esp32_mquickjs_memory_payload_free(storage);
    return mutex;
}

void esp32_mquickjs_memory_mutex_delete(SemaphoreHandle_t mutex)
{
    if (mutex == NULL) return;
    vSemaphoreDelete(mutex);
    esp32_mquickjs_memory_payload_free(mutex);
}

EventGroupHandle_t esp32_mquickjs_memory_event_group_create(const char *owner)
{
    StaticEventGroup_t *storage = memory_rtos_allocate(owner, sizeof(*storage), true);
    if (storage == NULL) return NULL;
    EventGroupHandle_t group = xEventGroupCreateStatic(storage);
    if (group == NULL) esp32_mquickjs_memory_payload_free(storage);
    return group;
}

void esp32_mquickjs_memory_event_group_delete(EventGroupHandle_t group)
{
    if (group == NULL) return;
    vEventGroupDelete(group);
    esp32_mquickjs_memory_payload_free(group);
}
