#include "esp32_mquickjs_websocket_client_resources.h"

#include <stddef.h>

#define WEBSOCKET_CLIENT_RESOURCES_INVALID (-1)

int esp32_mquickjs_websocket_client_resources_deinit(
    esp32_mquickjs_websocket_client_resources_t *resources,
    const esp32_mquickjs_websocket_client_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL) {
        return WEBSOCKET_CLIENT_RESOURCES_INVALID;
    }
    if (resources->client == NULL) {
        resources->started = false;
        resources->events_registered = false;
        return 0;
    }
    if (resources->started) {
        if (ops->stop == NULL) {
            return WEBSOCKET_CLIENT_RESOURCES_INVALID;
        }
        result = ops->stop(resources->client, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->started = false;
    }
    if (resources->events_registered) {
        if (ops->unregister_events == NULL) {
            return WEBSOCKET_CLIENT_RESOURCES_INVALID;
        }
        result = ops->unregister_events(resources->client, ops->opaque);
        if (result != 0) {
            return result;
        }
        resources->events_registered = false;
    }
    if (ops->destroy == NULL) {
        return WEBSOCKET_CLIENT_RESOURCES_INVALID;
    }
    result = ops->destroy(resources->client, ops->opaque);
    if (result != 0) {
        return result;
    }
    resources->client = NULL;
    return 0;
}
