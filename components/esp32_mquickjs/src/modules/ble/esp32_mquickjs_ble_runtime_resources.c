#include "esp32_mquickjs_ble_runtime_resources.h"

#include <stddef.h>

#define BLE_RUNTIME_RESOURCES_INVALID (-1)

int esp32_mquickjs_ble_runtime_resources_deinit(
    esp32_mquickjs_ble_runtime_resources_t *resources,
    const esp32_mquickjs_ble_runtime_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL) {
        return BLE_RUNTIME_RESOURCES_INVALID;
    }
    if (resources->host_started) {
        if (ops->stop_host == NULL) {
            return BLE_RUNTIME_RESOURCES_INVALID;
        }
        result = ops->stop_host(ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->host_started = false;
    }
    if (resources->port_initialized) {
        if (ops->deinit_port == NULL) {
            return BLE_RUNTIME_RESOURCES_INVALID;
        }
        result = ops->deinit_port(ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->port_initialized = false;
    }
    return 0;
}
