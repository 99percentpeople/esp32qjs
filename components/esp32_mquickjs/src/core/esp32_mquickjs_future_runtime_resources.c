#include "esp32_mquickjs_future_runtime_resources.h"

#include <string.h>

void esp32_mquickjs_future_runtime_resources_deinit(
    esp32_mquickjs_future_runtime_resources_t *resources,
    const esp32_mquickjs_future_runtime_resource_ops_t *ops)
{
    if (resources == NULL || ops == NULL) {
        return;
    }
    if (resources->ready != NULL && ops->queue_delete != NULL) {
        ops->queue_delete(resources->ready, ops->opaque);
        resources->ready = NULL;
    }
    if (resources->submissions != NULL && ops->queue_delete != NULL) {
        ops->queue_delete(resources->submissions, ops->opaque);
        resources->submissions = NULL;
    }
    if (resources->slots != NULL && ops->release != NULL) {
        ops->release(resources->slots, ops->opaque);
        resources->slots = NULL;
    }
    if (resources->runtime_state != NULL && ops->release != NULL) {
        ops->release(resources->runtime_state, ops->opaque);
        resources->runtime_state = NULL;
    }
}

bool esp32_mquickjs_future_runtime_resources_init(
    esp32_mquickjs_future_runtime_resources_t *resources,
    const esp32_mquickjs_future_runtime_resource_ops_t *ops,
    size_t runtime_state_size,
    size_t slot_count,
    size_t slot_size,
    size_t ready_queue_length,
    size_t token_size)
{
    if (resources == NULL || ops == NULL || ops->allocate == NULL ||
        ops->release == NULL || ops->queue_create == NULL ||
        ops->queue_delete == NULL || runtime_state_size == 0 ||
        slot_count == 0 || slot_size == 0 || ready_queue_length == 0 ||
        token_size == 0) {
        return false;
    }
    memset(resources, 0, sizeof(*resources));
    resources->runtime_state =
        ops->allocate(1, runtime_state_size, ops->opaque);
    if (resources->runtime_state == NULL) {
        goto fail;
    }
    resources->slots = ops->allocate(slot_count, slot_size, ops->opaque);
    if (resources->slots == NULL) {
        goto fail;
    }
    resources->submissions =
        ops->queue_create(slot_count, token_size, ops->opaque);
    if (resources->submissions == NULL) {
        goto fail;
    }
    resources->ready =
        ops->queue_create(ready_queue_length, token_size, ops->opaque);
    if (resources->ready == NULL) {
        goto fail;
    }
    return true;

fail:
    esp32_mquickjs_future_runtime_resources_deinit(resources, ops);
    return false;
}
