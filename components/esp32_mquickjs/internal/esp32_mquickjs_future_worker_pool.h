#pragma once

#include <stddef.h>

typedef void (*esp32_mquickjs_future_worker_cleanup_fn)(
    size_t worker_index,
    void *opaque);
typedef void (*esp32_mquickjs_future_queue_cleanup_fn)(void *opaque);

/** Release a partially initialized worker pool in reverse creation order. */
void esp32_mquickjs_future_worker_pool_cleanup_partial(
    size_t started_workers,
    esp32_mquickjs_future_worker_cleanup_fn cleanup_worker,
    esp32_mquickjs_future_queue_cleanup_fn cleanup_queue,
    void *opaque);
