#include "esp32_mquickjs_camera_driver_resources.h"

#include <stddef.h>

#define CAMERA_DRIVER_RESOURCES_INVALID (-1)

int esp32_mquickjs_camera_driver_resources_deinit(
    esp32_mquickjs_camera_driver_resources_t *resources,
    const esp32_mquickjs_camera_driver_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL || ops->deinit == NULL) {
        return CAMERA_DRIVER_RESOURCES_INVALID;
    }
    if (!resources->initialized) {
        return 0;
    }
    result = ops->deinit(ops->opaque);
    if (result != 0) {
        return result;
    }
    resources->initialized = false;
    return 0;
}
