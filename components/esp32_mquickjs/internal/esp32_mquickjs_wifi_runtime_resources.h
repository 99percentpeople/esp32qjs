#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef void *(*esp32_mquickjs_wifi_runtime_create_fn)(void *opaque);
typedef void *(*esp32_mquickjs_wifi_runtime_create_queue_fn)(
    size_t length, size_t item_size, void *opaque);
typedef void (*esp32_mquickjs_wifi_runtime_delete_fn)(
    void *value, void *opaque);

typedef struct {
    esp32_mquickjs_wifi_runtime_create_fn create_lock;
    esp32_mquickjs_wifi_runtime_delete_fn delete_lock;
    esp32_mquickjs_wifi_runtime_create_fn create_event_group;
    esp32_mquickjs_wifi_runtime_delete_fn delete_event_group;
    esp32_mquickjs_wifi_runtime_create_queue_fn create_queue;
    esp32_mquickjs_wifi_runtime_delete_fn delete_queue;
    void *opaque;
} esp32_mquickjs_wifi_runtime_resource_ops_t;

typedef struct {
    void *lock;
    void *event_group;
    void *scan_queue;
    void *connect_queue;
    void *driver_event_queue;
} esp32_mquickjs_wifi_runtime_resources_t;

bool esp32_mquickjs_wifi_runtime_resources_init(
    esp32_mquickjs_wifi_runtime_resources_t *resources,
    const esp32_mquickjs_wifi_runtime_resource_ops_t *ops,
    size_t scan_queue_length, size_t scan_event_size,
    size_t connect_queue_length, size_t connect_event_size,
    size_t driver_event_queue_length, size_t driver_event_size);

void esp32_mquickjs_wifi_runtime_resources_deinit(
    esp32_mquickjs_wifi_runtime_resources_t *resources,
    const esp32_mquickjs_wifi_runtime_resource_ops_t *ops);
