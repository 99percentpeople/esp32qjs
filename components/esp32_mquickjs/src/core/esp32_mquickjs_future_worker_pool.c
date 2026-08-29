#include "esp32_mquickjs_future_worker_pool.h"

void esp32_mquickjs_future_worker_pool_cleanup_partial(
    size_t started_workers,
    esp32_mquickjs_future_worker_cleanup_fn cleanup_worker,
    esp32_mquickjs_future_queue_cleanup_fn cleanup_queue,
    void *opaque)
{
    if (cleanup_worker != NULL) {
        while (started_workers > 0) {
            started_workers--;
            cleanup_worker(started_workers, opaque);
        }
    }
    if (cleanup_queue != NULL) {
        cleanup_queue(opaque);
    }
}
