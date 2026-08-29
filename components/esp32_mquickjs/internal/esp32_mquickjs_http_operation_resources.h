#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef void *(*esp32_mquickjs_http_operation_allocate_fn)(
    size_t size, void *opaque);
typedef void (*esp32_mquickjs_http_operation_release_fn)(
    void *value, void *opaque);
typedef void *(*esp32_mquickjs_http_operation_create_lock_fn)(void *opaque);
typedef void (*esp32_mquickjs_http_operation_delete_lock_fn)(
    void *lock, void *opaque);

typedef struct {
    esp32_mquickjs_http_operation_allocate_fn allocate;
    esp32_mquickjs_http_operation_release_fn release;
    esp32_mquickjs_http_operation_create_lock_fn create_lock;
    esp32_mquickjs_http_operation_delete_lock_fn delete_lock;
    void *opaque;
} esp32_mquickjs_http_operation_resource_ops_t;

typedef struct {
    void *operation;
    void *lock;
} esp32_mquickjs_http_operation_resources_t;

bool esp32_mquickjs_http_operation_resources_init(
    esp32_mquickjs_http_operation_resources_t *resources,
    const esp32_mquickjs_http_operation_resource_ops_t *ops,
    size_t operation_size);

void esp32_mquickjs_http_operation_resources_deinit(
    esp32_mquickjs_http_operation_resources_t *resources,
    const esp32_mquickjs_http_operation_resource_ops_t *ops);
