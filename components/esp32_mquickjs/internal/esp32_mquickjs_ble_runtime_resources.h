#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_ble_runtime_stop_host_fn)(void *opaque);
typedef int (*esp32_mquickjs_ble_runtime_deinit_port_fn)(void *opaque);

typedef struct {
    esp32_mquickjs_ble_runtime_stop_host_fn stop_host;
    esp32_mquickjs_ble_runtime_deinit_port_fn deinit_port;
    void *opaque;
} esp32_mquickjs_ble_runtime_resource_ops_t;

typedef struct {
    bool port_initialized;
    bool host_started;
} esp32_mquickjs_ble_runtime_resources_t;

int esp32_mquickjs_ble_runtime_resources_deinit(
    esp32_mquickjs_ble_runtime_resources_t *resources,
    const esp32_mquickjs_ble_runtime_resource_ops_t *ops);
