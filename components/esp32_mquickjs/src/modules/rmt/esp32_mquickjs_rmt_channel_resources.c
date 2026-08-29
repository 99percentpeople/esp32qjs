#include "esp32_mquickjs_rmt_channel_resources.h"

#include <stddef.h>
#include <string.h>

#define RMT_CHANNEL_RESOURCES_INVALID (-1)

int esp32_mquickjs_rmt_channel_resources_deinit(
    esp32_mquickjs_rmt_channel_resources_t *resources,
    const esp32_mquickjs_rmt_channel_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL) {
        return RMT_CHANNEL_RESOURCES_INVALID;
    }
    if (resources->enabled) {
        if (resources->channel == NULL || ops->disable_channel == NULL) {
            return RMT_CHANNEL_RESOURCES_INVALID;
        }
        result = ops->disable_channel(resources->channel, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->enabled = false;
    }
    if (resources->encoder != NULL) {
        if (ops->delete_encoder == NULL) {
            return RMT_CHANNEL_RESOURCES_INVALID;
        }
        result = ops->delete_encoder(resources->encoder, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->encoder = NULL;
    }
    if (resources->channel != NULL) {
        if (ops->delete_channel == NULL) {
            return RMT_CHANNEL_RESOURCES_INVALID;
        }
        result = ops->delete_channel(resources->channel, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->channel = NULL;
    }
    return 0;
}

int esp32_mquickjs_rmt_channel_resources_init(
    esp32_mquickjs_rmt_channel_resources_t *resources,
    bool create_encoder,
    const esp32_mquickjs_rmt_channel_resource_ops_t *ops)
{
    int result;
    int cleanup_result;

    if (resources == NULL || ops == NULL || ops->create_channel == NULL ||
        ops->register_callbacks == NULL || ops->disable_channel == NULL ||
        ops->delete_channel == NULL ||
        (create_encoder &&
         (ops->create_encoder == NULL || ops->delete_encoder == NULL))) {
        return RMT_CHANNEL_RESOURCES_INVALID;
    }
    memset(resources, 0, sizeof(*resources));
    result = ops->create_channel(ops->opaque, &resources->channel);
    if (result != 0 || resources->channel == NULL) {
        if (result == 0) {
            result = RMT_CHANNEL_RESOURCES_INVALID;
        }
        goto fail;
    }
    if (create_encoder) {
        result = ops->create_encoder(ops->opaque, &resources->encoder);
        if (result != 0 || resources->encoder == NULL) {
            if (result == 0) {
                result = RMT_CHANNEL_RESOURCES_INVALID;
            }
            goto fail;
        }
    }
    result = ops->register_callbacks(resources->channel, ops->opaque);
    if (result != 0) {
        goto fail;
    }
    return 0;

fail:
    cleanup_result = esp32_mquickjs_rmt_channel_resources_deinit(resources, ops);
    return cleanup_result != 0 ? cleanup_result : result;
}
