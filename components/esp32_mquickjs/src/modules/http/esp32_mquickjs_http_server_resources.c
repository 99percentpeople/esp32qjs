#include "esp32_mquickjs_http_server_resources.h"

#include <stddef.h>

#define HTTP_SERVER_RESOURCES_INVALID (-1)

int esp32_mquickjs_http_server_resources_stop(
    esp32_mquickjs_http_server_resources_t *resources,
    const esp32_mquickjs_http_server_resource_ops_t *ops)
{
    int result;

    if (resources == NULL || ops == NULL || ops->stop == NULL) {
        return HTTP_SERVER_RESOURCES_INVALID;
    }
    if (resources->handle == NULL) {
        return 0;
    }
    result = ops->stop(resources->handle, ops->opaque);
    if (result != 0) {
        return result;
    }
    resources->handle = NULL;
    return 0;
}
