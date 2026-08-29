#include "esp32_mquickjs_uart_port_resources.h"

#include <stddef.h>
#include <string.h>

#define UART_PORT_RESOURCES_INVALID (-1)

int esp32_mquickjs_uart_port_resources_deinit(
    esp32_mquickjs_uart_port_resources_t *resources,
    const esp32_mquickjs_uart_port_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL) {
        return UART_PORT_RESOURCES_INVALID;
    }
    if (resources->driver_installed) {
        if (ops->delete_driver == NULL) {
            return UART_PORT_RESOURCES_INVALID;
        }
        result = ops->delete_driver(ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->driver_installed = false;
        resources->driver_events = NULL;
    }
    resources->driver_events = NULL;
    if (resources->watch_lock != NULL) {
        if (ops->delete_watch_lock == NULL) {
            return UART_PORT_RESOURCES_INVALID;
        }
        ops->delete_watch_lock(resources->watch_lock, ops->opaque);
        resources->watch_lock = NULL;
    }
    return 0;
}

int esp32_mquickjs_uart_port_resources_init(
    esp32_mquickjs_uart_port_resources_t *resources,
    const esp32_mquickjs_uart_port_resource_ops_t *ops)
{
    int result;
    int cleanup_result;

    if (resources == NULL || ops == NULL ||
        ops->create_watch_lock == NULL || ops->install_driver == NULL ||
        ops->delete_driver == NULL || ops->delete_watch_lock == NULL) {
        return UART_PORT_RESOURCES_INVALID;
    }
    memset(resources, 0, sizeof(*resources));
    result = ops->create_watch_lock(ops->opaque, &resources->watch_lock);
    if (result != 0 || resources->watch_lock == NULL) {
        if (result == 0) {
            result = UART_PORT_RESOURCES_INVALID;
        }
        goto fail;
    }
    result = ops->install_driver(ops->opaque, &resources->driver_events);
    if (result != 0) {
        goto fail;
    }
    resources->driver_installed = true;
    if (resources->driver_events == NULL) {
        result = UART_PORT_RESOURCES_INVALID;
        goto fail;
    }
    return 0;

fail:
    cleanup_result =
        esp32_mquickjs_uart_port_resources_deinit(resources, ops);
    return cleanup_result != 0 ? cleanup_result : result;
}
