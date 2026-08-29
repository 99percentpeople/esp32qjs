#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef void *(*esp32_mquickjs_future_runtime_allocate_fn)(
    size_t count, size_t size, void *opaque);
typedef void (*esp32_mquickjs_future_runtime_release_fn)(
    void *value, void *opaque);
typedef void *(*esp32_mquickjs_future_runtime_queue_create_fn)(
    size_t length, size_t item_size, void *opaque);
typedef void (*esp32_mquickjs_future_runtime_queue_delete_fn)(
    void *queue, void *opaque);

typedef struct {
    esp32_mquickjs_future_runtime_allocate_fn allocate;
    esp32_mquickjs_future_runtime_release_fn release;
    esp32_mquickjs_future_runtime_queue_create_fn queue_create;
    esp32_mquickjs_future_runtime_queue_delete_fn queue_delete;
    void *opaque;
} esp32_mquickjs_future_runtime_resource_ops_t;

typedef struct {
    void *runtime_state;
    void *slots;
    void *submissions;
    void *ready;
} esp32_mquickjs_future_runtime_resources_t;

bool esp32_mquickjs_future_runtime_resources_init(
    esp32_mquickjs_future_runtime_resources_t *resources,
    const esp32_mquickjs_future_runtime_resource_ops_t *ops,
    size_t runtime_state_size,
    size_t slot_count,
    size_t slot_size,
    size_t ready_queue_length,
    size_t token_size);

void esp32_mquickjs_future_runtime_resources_deinit(
    esp32_mquickjs_future_runtime_resources_t *resources,
    const esp32_mquickjs_future_runtime_resource_ops_t *ops);
