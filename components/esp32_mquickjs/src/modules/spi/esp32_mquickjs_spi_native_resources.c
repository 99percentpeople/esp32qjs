#include "esp32_mquickjs_spi_native_resources.h"

#include <stddef.h>

#define SPI_NATIVE_RESOURCES_INVALID (-1)

int esp32_mquickjs_spi_device_resources_deinit(
    esp32_mquickjs_spi_device_resources_t *resources,
    const esp32_mquickjs_spi_device_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL || ops->remove == NULL) {
        return SPI_NATIVE_RESOURCES_INVALID;
    }
    if (resources->handle == NULL) {
        return 0;
    }
    result = ops->remove(resources->handle, ops->opaque);
    if (result != 0) {
        return result;
    }
    resources->handle = NULL;
    return 0;
}

int esp32_mquickjs_spi_bus_resources_deinit(
    esp32_mquickjs_spi_bus_resources_t *resources,
    const esp32_mquickjs_spi_bus_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL || ops->free == NULL) {
        return SPI_NATIVE_RESOURCES_INVALID;
    }
    if (!resources->initialized) {
        return 0;
    }
    result = ops->free(resources->host_id, ops->opaque);
    if (result != 0) {
        return result;
    }
    resources->initialized = false;
    return 0;
}
