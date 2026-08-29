#pragma once

#include <stdbool.h>

#define ESP32_MQUICKJS_I2C_BUS_RESOURCE_UNAVAILABLE (-2)

typedef bool (*esp32_mquickjs_i2c_bus_acquire_lease_fn)(void *opaque);
typedef void (*esp32_mquickjs_i2c_bus_release_lease_fn)(void *opaque);
typedef int (*esp32_mquickjs_i2c_bus_create_fn)(
    void *opaque, void **out_bus);
typedef int (*esp32_mquickjs_i2c_bus_delete_fn)(
    void *bus, void *opaque);

typedef struct {
    esp32_mquickjs_i2c_bus_acquire_lease_fn acquire_lease;
    esp32_mquickjs_i2c_bus_release_lease_fn release_lease;
    esp32_mquickjs_i2c_bus_create_fn create_bus;
    esp32_mquickjs_i2c_bus_delete_fn delete_bus;
    void *opaque;
} esp32_mquickjs_i2c_bus_resource_ops_t;

typedef struct {
    void *bus;
    bool lease_acquired;
} esp32_mquickjs_i2c_bus_resources_t;

int esp32_mquickjs_i2c_bus_resources_init(
    esp32_mquickjs_i2c_bus_resources_t *resources,
    const esp32_mquickjs_i2c_bus_resource_ops_t *ops);

int esp32_mquickjs_i2c_bus_resources_deinit(
    esp32_mquickjs_i2c_bus_resources_t *resources,
    const esp32_mquickjs_i2c_bus_resource_ops_t *ops);
