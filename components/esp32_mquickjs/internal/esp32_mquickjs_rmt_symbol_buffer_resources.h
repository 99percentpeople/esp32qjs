#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef void *(*esp32_mquickjs_rmt_symbol_buffer_allocate_fn)(
    size_t size, void *opaque);
typedef void (*esp32_mquickjs_rmt_symbol_buffer_release_fn)(
    void *value, void *opaque);

typedef struct {
    esp32_mquickjs_rmt_symbol_buffer_allocate_fn allocate_buffer;
    esp32_mquickjs_rmt_symbol_buffer_allocate_fn allocate_symbols;
    esp32_mquickjs_rmt_symbol_buffer_release_fn release;
    void *opaque;
} esp32_mquickjs_rmt_symbol_buffer_resource_ops_t;

typedef struct {
    void *buffer;
    void *symbols;
} esp32_mquickjs_rmt_symbol_buffer_resources_t;

bool esp32_mquickjs_rmt_symbol_buffer_resources_init(
    esp32_mquickjs_rmt_symbol_buffer_resources_t *resources,
    const esp32_mquickjs_rmt_symbol_buffer_resource_ops_t *ops,
    size_t buffer_size, size_t symbol_count, size_t symbol_size);

void esp32_mquickjs_rmt_symbol_buffer_resources_deinit(
    esp32_mquickjs_rmt_symbol_buffer_resources_t *resources,
    const esp32_mquickjs_rmt_symbol_buffer_resource_ops_t *ops);
