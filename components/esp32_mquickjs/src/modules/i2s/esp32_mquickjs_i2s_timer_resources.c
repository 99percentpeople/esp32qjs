#include "esp32_mquickjs_i2s_timer_resources.h"

#include <string.h>

#define I2S_TIMER_RESOURCES_INVALID (-1)

void esp32_mquickjs_i2s_timer_resources_deinit(
    esp32_mquickjs_i2s_timer_resources_t *resources,
    const esp32_mquickjs_i2s_timer_resource_ops_t *ops)
{
    if (resources == NULL || ops == NULL || ops->delete_timer == NULL) {
        return;
    }
    if (resources->tx_timer != NULL) {
        ops->delete_timer(resources->tx_timer, ops->opaque);
        resources->tx_timer = NULL;
    }
    if (resources->rx_timer != NULL) {
        ops->delete_timer(resources->rx_timer, ops->opaque);
        resources->rx_timer = NULL;
    }
}

int esp32_mquickjs_i2s_timer_resources_init(
    esp32_mquickjs_i2s_timer_resources_t *resources,
    const esp32_mquickjs_i2s_timer_resource_ops_t *ops,
    bool need_rx, bool need_tx)
{
    int result;

    if (resources == NULL || ops == NULL || ops->delete_timer == NULL ||
        (need_rx && ops->create_rx == NULL) ||
        (need_tx && ops->create_tx == NULL)) {
        return I2S_TIMER_RESOURCES_INVALID;
    }
    memset(resources, 0, sizeof(*resources));
    if (need_rx) {
        result = ops->create_rx(ops->opaque, &resources->rx_timer);
        if (result != 0 || resources->rx_timer == NULL) {
            if (result == 0) {
                result = I2S_TIMER_RESOURCES_INVALID;
            }
            goto fail;
        }
    }
    if (need_tx) {
        result = ops->create_tx(ops->opaque, &resources->tx_timer);
        if (result != 0 || resources->tx_timer == NULL) {
            if (result == 0) {
                result = I2S_TIMER_RESOURCES_INVALID;
            }
            goto fail;
        }
    }
    return 0;

fail:
    esp32_mquickjs_i2s_timer_resources_deinit(resources, ops);
    return result;
}
