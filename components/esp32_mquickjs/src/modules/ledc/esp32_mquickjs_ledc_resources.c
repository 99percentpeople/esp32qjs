#include "esp32_mquickjs_ledc_resources.h"

#include <stddef.h>

#define LEDC_RESOURCES_INVALID (-1)

int esp32_mquickjs_ledc_channel_resources_deinit(
    esp32_mquickjs_ledc_channel_resources_t *resources,
    int channel,
    const esp32_mquickjs_ledc_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL || ops->stop_channel == NULL ||
        ops->deconfigure_channel == NULL) {
        return LEDC_RESOURCES_INVALID;
    }
    if (!resources->configured) {
        return 0;
    }
    if (!resources->stopped) {
        result = ops->stop_channel(channel, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->stopped = true;
    }
    result = ops->deconfigure_channel(channel, ops->opaque);
    if (result != 0) {
        return result;
    }
    resources->configured = false;
    resources->stopped = false;
    return 0;
}

int esp32_mquickjs_ledc_timer_resources_deinit(
    esp32_mquickjs_ledc_timer_resources_t *resources,
    int timer,
    const esp32_mquickjs_ledc_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL || ops->pause_timer == NULL ||
        ops->deconfigure_timer == NULL) {
        return LEDC_RESOURCES_INVALID;
    }
    if (!resources->configured) {
        return 0;
    }
    if (!resources->paused) {
        result = ops->pause_timer(timer, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->paused = true;
    }
    result = ops->deconfigure_timer(timer, ops->opaque);
    if (result != 0) {
        return result;
    }
    resources->configured = false;
    resources->paused = false;
    return 0;
}
