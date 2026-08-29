#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_spi_device_remove_fn)(
    void *device, void *opaque);
typedef int (*esp32_mquickjs_spi_bus_free_fn)(int host_id, void *opaque);

typedef struct {
    esp32_mquickjs_spi_device_remove_fn remove;
    void *opaque;
} esp32_mquickjs_spi_device_resource_ops_t;

typedef struct {
    esp32_mquickjs_spi_bus_free_fn free;
    void *opaque;
} esp32_mquickjs_spi_bus_resource_ops_t;

typedef struct {
    void *handle;
} esp32_mquickjs_spi_device_resources_t;

typedef struct {
    bool initialized;
    int host_id;
} esp32_mquickjs_spi_bus_resources_t;

int esp32_mquickjs_spi_device_resources_deinit(
    esp32_mquickjs_spi_device_resources_t *resources,
    const esp32_mquickjs_spi_device_resource_ops_t *ops);

int esp32_mquickjs_spi_bus_resources_deinit(
    esp32_mquickjs_spi_bus_resources_t *resources,
    const esp32_mquickjs_spi_bus_resource_ops_t *ops);
