#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_timer_resource_create_fn)(
    void *opaque, void **out_timer);
typedef int (*esp32_mquickjs_timer_resource_start_fn)(
    void *timer, void *opaque);
typedef void (*esp32_mquickjs_timer_resource_stop_fn)(
    void *timer, void *opaque);
typedef void (*esp32_mquickjs_timer_resource_delete_fn)(
    void *timer, void *opaque);

typedef struct {
    esp32_mquickjs_timer_resource_create_fn create;
    esp32_mquickjs_timer_resource_start_fn start;
    esp32_mquickjs_timer_resource_stop_fn stop;
    esp32_mquickjs_timer_resource_delete_fn delete_timer;
    void *opaque;
} esp32_mquickjs_timer_resource_ops_t;

typedef struct {
    void *timer;
    bool started;
} esp32_mquickjs_timer_resource_t;

int esp32_mquickjs_timer_resource_acquire(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops);

int esp32_mquickjs_timer_resource_start(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops);

void esp32_mquickjs_timer_resource_stop(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops);

int esp32_mquickjs_timer_resource_init(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops);

void esp32_mquickjs_timer_resource_deinit(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops);
