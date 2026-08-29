#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *drain_scratch;
    void *overflow_scratch;
    void *send_lock;
    void *events;
} esp32_mquickjs_event_queue_resources_t;

typedef struct {
    void *(*allocate)(size_t bytes, void *opaque);
    void (*release)(void *allocation, void *opaque);
    void *(*create_lock)(void *opaque);
    void (*delete_lock)(void *lock, void *opaque);
    void *(*create_queue)(uint32_t capacity,
                          size_t event_size,
                          void *opaque);
    void (*delete_queue)(void *queue, void *opaque);
} esp32_mquickjs_event_queue_resource_ops_t;

/**
 * Allocate every EventQueue-owned native resource or roll back the partial
 * result in reverse ownership order. The output is empty after failure.
 */
bool esp32_mquickjs_event_queue_resources_init(
    esp32_mquickjs_event_queue_resources_t *resources,
    size_t event_size,
    uint32_t capacity,
    bool needs_overflow_scratch,
    const esp32_mquickjs_event_queue_resource_ops_t *ops,
    void *opaque);

void esp32_mquickjs_event_queue_resources_deinit(
    esp32_mquickjs_event_queue_resources_t *resources,
    const esp32_mquickjs_event_queue_resource_ops_t *ops,
    void *opaque);
