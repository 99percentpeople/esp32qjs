#include "esp32_mquickjs_wifi_runtime_resources.h"

#include <string.h>

void esp32_mquickjs_wifi_runtime_resources_deinit(
    esp32_mquickjs_wifi_runtime_resources_t *resources,
    const esp32_mquickjs_wifi_runtime_resource_ops_t *ops)
{
    if (resources == NULL || ops == NULL) {
        return;
    }
    if (resources->driver_event_queue != NULL &&
        ops->delete_queue != NULL) {
        ops->delete_queue(resources->driver_event_queue, ops->opaque);
        resources->driver_event_queue = NULL;
    }
    if (resources->connect_queue != NULL && ops->delete_queue != NULL) {
        ops->delete_queue(resources->connect_queue, ops->opaque);
        resources->connect_queue = NULL;
    }
    if (resources->scan_queue != NULL && ops->delete_queue != NULL) {
        ops->delete_queue(resources->scan_queue, ops->opaque);
        resources->scan_queue = NULL;
    }
    if (resources->event_group != NULL &&
        ops->delete_event_group != NULL) {
        ops->delete_event_group(resources->event_group, ops->opaque);
        resources->event_group = NULL;
    }
    if (resources->lock != NULL && ops->delete_lock != NULL) {
        ops->delete_lock(resources->lock, ops->opaque);
        resources->lock = NULL;
    }
}

bool esp32_mquickjs_wifi_runtime_resources_init(
    esp32_mquickjs_wifi_runtime_resources_t *resources,
    const esp32_mquickjs_wifi_runtime_resource_ops_t *ops,
    size_t scan_queue_length, size_t scan_event_size,
    size_t connect_queue_length, size_t connect_event_size,
    size_t driver_event_queue_length, size_t driver_event_size)
{
    if (resources == NULL || ops == NULL || ops->create_lock == NULL ||
        ops->delete_lock == NULL || ops->create_event_group == NULL ||
        ops->delete_event_group == NULL || ops->create_queue == NULL ||
        ops->delete_queue == NULL || scan_queue_length == 0 ||
        scan_event_size == 0 || connect_queue_length == 0 ||
        connect_event_size == 0 || driver_event_queue_length == 0 ||
        driver_event_size == 0) {
        return false;
    }
    memset(resources, 0, sizeof(*resources));
    resources->lock = ops->create_lock(ops->opaque);
    if (resources->lock == NULL) {
        goto fail;
    }
    resources->event_group = ops->create_event_group(ops->opaque);
    if (resources->event_group == NULL) {
        goto fail;
    }
    resources->scan_queue = ops->create_queue(
        scan_queue_length, scan_event_size, ops->opaque);
    if (resources->scan_queue == NULL) {
        goto fail;
    }
    resources->connect_queue = ops->create_queue(
        connect_queue_length, connect_event_size, ops->opaque);
    if (resources->connect_queue == NULL) {
        goto fail;
    }
    resources->driver_event_queue = ops->create_queue(
        driver_event_queue_length, driver_event_size, ops->opaque);
    if (resources->driver_event_queue == NULL) {
        goto fail;
    }
    return true;

fail:
    esp32_mquickjs_wifi_runtime_resources_deinit(resources, ops);
    return false;
}
