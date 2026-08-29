#include "esp32_mquickjs_rmt_symbol_buffer_resources.h"

#include <stdint.h>
#include <string.h>

void esp32_mquickjs_rmt_symbol_buffer_resources_deinit(
    esp32_mquickjs_rmt_symbol_buffer_resources_t *resources,
    const esp32_mquickjs_rmt_symbol_buffer_resource_ops_t *ops)
{
    if (resources == NULL || ops == NULL || ops->release == NULL) {
        return;
    }
    if (resources->symbols != NULL) {
        ops->release(resources->symbols, ops->opaque);
        resources->symbols = NULL;
    }
    if (resources->buffer != NULL) {
        ops->release(resources->buffer, ops->opaque);
        resources->buffer = NULL;
    }
}

bool esp32_mquickjs_rmt_symbol_buffer_resources_init(
    esp32_mquickjs_rmt_symbol_buffer_resources_t *resources,
    const esp32_mquickjs_rmt_symbol_buffer_resource_ops_t *ops,
    size_t buffer_size, size_t symbol_count, size_t symbol_size)
{
    size_t symbols_size;

    if (resources == NULL || ops == NULL || ops->allocate_buffer == NULL ||
        ops->allocate_symbols == NULL || ops->release == NULL ||
        buffer_size == 0 || symbol_count == 0 || symbol_size == 0 ||
        symbol_count > SIZE_MAX / symbol_size) {
        return false;
    }
    memset(resources, 0, sizeof(*resources));
    symbols_size = symbol_count * symbol_size;
    resources->buffer = ops->allocate_buffer(buffer_size, ops->opaque);
    if (resources->buffer == NULL) {
        goto fail;
    }
    resources->symbols = ops->allocate_symbols(symbols_size, ops->opaque);
    if (resources->symbols == NULL) {
        goto fail;
    }
    return true;

fail:
    esp32_mquickjs_rmt_symbol_buffer_resources_deinit(resources, ops);
    return false;
}
