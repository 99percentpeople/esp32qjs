#include "esp32_mquickjs_i2s_channel_resources.h"

#include <stddef.h>
#include <string.h>

#define I2S_CHANNEL_RESOURCES_INVALID (-1)

static bool i2s_channel_resources_valid(
    const esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops)
{
    return resources != NULL && ops != NULL &&
           ops->enable_channel != NULL && ops->disable_channel != NULL &&
           ops->delete_channel != NULL;
}

int esp32_mquickjs_i2s_channel_resources_start(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops)
{
    bool tx_started = false;
    int result;
    int cleanup_result;

    if (!i2s_channel_resources_valid(resources, ops) ||
        (resources->rx_channel == NULL && resources->tx_channel == NULL)) {
        return I2S_CHANNEL_RESOURCES_INVALID;
    }
    if (resources->tx_channel != NULL && !resources->tx_enabled) {
        result = ops->enable_channel(resources->tx_channel, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->tx_enabled = true;
        tx_started = true;
    }
    if (resources->rx_channel != NULL && !resources->rx_enabled) {
        result = ops->enable_channel(resources->rx_channel, ops->opaque);
        if (result != 0) {
            if (!tx_started) {
                return result;
            }
            cleanup_result =
                ops->disable_channel(resources->tx_channel, ops->opaque);
            if (cleanup_result != 0) {
                return cleanup_result;
            }
            resources->tx_enabled = false;
            return result;
        }
        resources->rx_enabled = true;
    }
    return 0;
}

int esp32_mquickjs_i2s_channel_resources_stop(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops)
{
    int result;

    if (!i2s_channel_resources_valid(resources, ops)) {
        return I2S_CHANNEL_RESOURCES_INVALID;
    }
    if (resources->rx_enabled) {
        if (resources->rx_channel == NULL) {
            return I2S_CHANNEL_RESOURCES_INVALID;
        }
        result = ops->disable_channel(resources->rx_channel, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->rx_enabled = false;
    }
    if (resources->tx_enabled) {
        if (resources->tx_channel == NULL) {
            return I2S_CHANNEL_RESOURCES_INVALID;
        }
        result = ops->disable_channel(resources->tx_channel, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->tx_enabled = false;
    }
    return 0;
}

int esp32_mquickjs_i2s_channel_resources_delete(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops)
{
    int result;

    if (!i2s_channel_resources_valid(resources, ops) ||
        resources->rx_enabled || resources->tx_enabled) {
        return I2S_CHANNEL_RESOURCES_INVALID;
    }
    if (resources->rx_channel != NULL) {
        result = ops->delete_channel(resources->rx_channel, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->rx_channel = NULL;
    }
    if (resources->tx_channel != NULL) {
        result = ops->delete_channel(resources->tx_channel, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->tx_channel = NULL;
    }
    return 0;
}

int esp32_mquickjs_i2s_channel_resources_deinit(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops)
{
    int result;

    result = esp32_mquickjs_i2s_channel_resources_stop(resources, ops);
    if (result != 0) {
        return result;
    }
    return esp32_mquickjs_i2s_channel_resources_delete(resources, ops);
}

int esp32_mquickjs_i2s_channel_resources_init(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    bool need_rx, bool need_tx,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops)
{
    int result;
    int cleanup_result;

    if (!i2s_channel_resources_valid(resources, ops) ||
        ops->create_channels == NULL || (!need_rx && !need_tx)) {
        return I2S_CHANNEL_RESOURCES_INVALID;
    }
    memset(resources, 0, sizeof(*resources));
    result = ops->create_channels(
        ops->opaque, need_tx ? &resources->tx_channel : NULL,
        need_rx ? &resources->rx_channel : NULL);
    if (result == 0 &&
        ((need_tx && resources->tx_channel == NULL) ||
         (need_rx && resources->rx_channel == NULL))) {
        result = I2S_CHANNEL_RESOURCES_INVALID;
    }
    if (result == 0) {
        return 0;
    }
    cleanup_result =
        esp32_mquickjs_i2s_channel_resources_deinit(resources, ops);
    return cleanup_result != 0 ? cleanup_result : result;
}
