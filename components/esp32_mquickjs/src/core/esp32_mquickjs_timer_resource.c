#include "esp32_mquickjs_timer_resource.h"

#include <stddef.h>
#include <string.h>

#define TIMER_RESOURCE_INVALID (-1)

void esp32_mquickjs_timer_resource_stop(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops)
{
    if (resource == NULL || ops == NULL || resource->timer == NULL ||
        !resource->started) {
        return;
    }
    if (ops->stop != NULL) {
        ops->stop(resource->timer, ops->opaque);
    }
    resource->started = false;
}

void esp32_mquickjs_timer_resource_deinit(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops)
{
    if (resource == NULL || ops == NULL) {
        return;
    }
    if (resource->timer == NULL) {
        resource->started = false;
        return;
    }
    esp32_mquickjs_timer_resource_stop(resource, ops);
    if (ops->delete_timer != NULL) {
        ops->delete_timer(resource->timer, ops->opaque);
        resource->timer = NULL;
    }
}

int esp32_mquickjs_timer_resource_acquire(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops)
{
    int result;

    if (resource == NULL || ops == NULL || ops->create == NULL ||
        ops->stop == NULL || ops->delete_timer == NULL ||
        resource->timer != NULL) {
        return TIMER_RESOURCE_INVALID;
    }
    resource->started = false;
    result = ops->create(ops->opaque, &resource->timer);
    if (result != 0 || resource->timer == NULL) {
        if (result == 0) {
            result = TIMER_RESOURCE_INVALID;
        }
        esp32_mquickjs_timer_resource_deinit(resource, ops);
        return result;
    }
    return 0;
}

int esp32_mquickjs_timer_resource_start(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops)
{
    int result;

    if (resource == NULL || ops == NULL || ops->start == NULL ||
        resource->timer == NULL || resource->started) {
        return TIMER_RESOURCE_INVALID;
    }
    result = ops->start(resource->timer, ops->opaque);
    if (result != 0) {
        return result;
    }
    resource->started = true;
    return 0;
}

int esp32_mquickjs_timer_resource_init(
    esp32_mquickjs_timer_resource_t *resource,
    const esp32_mquickjs_timer_resource_ops_t *ops)
{
    int result;

    if (resource == NULL) {
        return TIMER_RESOURCE_INVALID;
    }
    memset(resource, 0, sizeof(*resource));
    result = esp32_mquickjs_timer_resource_acquire(resource, ops);
    if (result == 0) {
        result = esp32_mquickjs_timer_resource_start(resource, ops);
    }
    if (result != 0) {
        esp32_mquickjs_timer_resource_deinit(resource, ops);
    }
    return result;
}
