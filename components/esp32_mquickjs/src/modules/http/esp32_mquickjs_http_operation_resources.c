#include "esp32_mquickjs_http_operation_resources.h"

#include <string.h>

void esp32_mquickjs_http_operation_resources_deinit(
    esp32_mquickjs_http_operation_resources_t *resources,
    const esp32_mquickjs_http_operation_resource_ops_t *ops)
{
    if (resources == NULL || ops == NULL) {
        return;
    }
    if (resources->lock != NULL && ops->delete_lock != NULL) {
        ops->delete_lock(resources->lock, ops->opaque);
        resources->lock = NULL;
    }
    if (resources->operation != NULL && ops->release != NULL) {
        ops->release(resources->operation, ops->opaque);
        resources->operation = NULL;
    }
}

bool esp32_mquickjs_http_operation_resources_init(
    esp32_mquickjs_http_operation_resources_t *resources,
    const esp32_mquickjs_http_operation_resource_ops_t *ops,
    size_t operation_size)
{
    if (resources == NULL || ops == NULL || ops->allocate == NULL ||
        ops->release == NULL || ops->create_lock == NULL ||
        ops->delete_lock == NULL || operation_size == 0) {
        return false;
    }
    memset(resources, 0, sizeof(*resources));
    resources->operation = ops->allocate(operation_size, ops->opaque);
    if (resources->operation == NULL) {
        goto fail;
    }
    resources->lock = ops->create_lock(ops->opaque);
    if (resources->lock == NULL) {
        goto fail;
    }
    return true;

fail:
    esp32_mquickjs_http_operation_resources_deinit(resources, ops);
    return false;
}
