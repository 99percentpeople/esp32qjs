#include "esp32_mquickjs_http_client_resources.h"

#include <stddef.h>

#define HTTP_CLIENT_RESOURCES_INVALID (-1)

int esp32_mquickjs_http_client_resources_deinit(
    esp32_mquickjs_http_client_resources_t *resources,
    const esp32_mquickjs_http_client_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL) {
        return HTTP_CLIENT_RESOURCES_INVALID;
    }
    if (resources->client == NULL) {
        return 0;
    }
    if (ops->cleanup == NULL) {
        return HTTP_CLIENT_RESOURCES_INVALID;
    }
    result = ops->cleanup(resources->client, ops->opaque);
    if (result != 0) {
        return result;
    }
    resources->client = NULL;
    return 0;
}
