#include "esp32_mquickjs_i2c_bus_resources.h"

#include <stddef.h>
#include <string.h>

#define I2C_BUS_RESOURCE_INVALID (-1)

int esp32_mquickjs_i2c_bus_resources_deinit(
    esp32_mquickjs_i2c_bus_resources_t *resources,
    const esp32_mquickjs_i2c_bus_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL) {
        return I2C_BUS_RESOURCE_INVALID;
    }
    if (resources->bus != NULL) {
        if (ops->delete_bus == NULL) {
            return I2C_BUS_RESOURCE_INVALID;
        }
        result = ops->delete_bus(resources->bus, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->bus = NULL;
    }
    if (resources->lease_acquired) {
        if (ops->release_lease == NULL) {
            return I2C_BUS_RESOURCE_INVALID;
        }
        ops->release_lease(ops->opaque);
        resources->lease_acquired = false;
    }
    return 0;
}

int esp32_mquickjs_i2c_bus_resources_init(
    esp32_mquickjs_i2c_bus_resources_t *resources,
    const esp32_mquickjs_i2c_bus_resource_ops_t *ops)
{
    int result;
    int cleanup_result;

    if (resources == NULL || ops == NULL || ops->acquire_lease == NULL ||
        ops->release_lease == NULL || ops->create_bus == NULL ||
        ops->delete_bus == NULL) {
        return I2C_BUS_RESOURCE_INVALID;
    }
    memset(resources, 0, sizeof(*resources));
    if (!ops->acquire_lease(ops->opaque)) {
        return ESP32_MQUICKJS_I2C_BUS_RESOURCE_UNAVAILABLE;
    }
    resources->lease_acquired = true;
    result = ops->create_bus(ops->opaque, &resources->bus);
    if (result == 0 && resources->bus != NULL) {
        return 0;
    }
    if (result == 0) {
        result = I2C_BUS_RESOURCE_INVALID;
    }
    cleanup_result = esp32_mquickjs_i2c_bus_resources_deinit(resources, ops);
    return cleanup_result != 0 ? cleanup_result : result;
}
