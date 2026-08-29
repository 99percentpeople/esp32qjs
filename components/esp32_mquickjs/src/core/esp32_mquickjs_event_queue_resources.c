#include "esp32_mquickjs_event_queue_resources.h"

#include <string.h>

void esp32_mquickjs_event_queue_resources_deinit(
    esp32_mquickjs_event_queue_resources_t *resources,
    const esp32_mquickjs_event_queue_resource_ops_t *ops,
    void *opaque)
{
    if (resources == NULL || ops == NULL) {
        return;
    }
    if (resources->events != NULL && ops->delete_queue != NULL) {
        ops->delete_queue(resources->events, opaque);
    }
    if (resources->send_lock != NULL && ops->delete_lock != NULL) {
        ops->delete_lock(resources->send_lock, opaque);
    }
    if (resources->overflow_scratch != NULL && ops->release != NULL) {
        ops->release(resources->overflow_scratch, opaque);
    }
    if (resources->drain_scratch != NULL && ops->release != NULL) {
        ops->release(resources->drain_scratch, opaque);
    }
    memset(resources, 0, sizeof(*resources));
}

bool esp32_mquickjs_event_queue_resources_init(
    esp32_mquickjs_event_queue_resources_t *resources,
    size_t event_size,
    uint32_t capacity,
    bool needs_overflow_scratch,
    const esp32_mquickjs_event_queue_resource_ops_t *ops,
    void *opaque)
{
    if (resources == NULL || event_size == 0 || capacity == 0 ||
        ops == NULL || ops->allocate == NULL || ops->release == NULL ||
        ops->create_lock == NULL || ops->delete_lock == NULL ||
        ops->create_queue == NULL || ops->delete_queue == NULL) {
        return false;
    }
    memset(resources, 0, sizeof(*resources));
    resources->drain_scratch = ops->allocate(event_size, opaque);
    if (resources->drain_scratch == NULL) {
        goto failed;
    }
    if (needs_overflow_scratch) {
        resources->overflow_scratch = ops->allocate(event_size, opaque);
        if (resources->overflow_scratch == NULL) {
            goto failed;
        }
    }
    resources->send_lock = ops->create_lock(opaque);
    if (resources->send_lock == NULL) {
        goto failed;
    }
    resources->events = ops->create_queue(capacity, event_size, opaque);
    if (resources->events == NULL) {
        goto failed;
    }
    return true;

failed:
    esp32_mquickjs_event_queue_resources_deinit(resources, ops, opaque);
    return false;
}
