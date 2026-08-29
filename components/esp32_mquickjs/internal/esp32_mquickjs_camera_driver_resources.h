#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_camera_driver_deinit_fn)(void *opaque);

typedef struct {
    esp32_mquickjs_camera_driver_deinit_fn deinit;
    void *opaque;
} esp32_mquickjs_camera_driver_resource_ops_t;

typedef struct {
    bool initialized;
} esp32_mquickjs_camera_driver_resources_t;

int esp32_mquickjs_camera_driver_resources_deinit(
    esp32_mquickjs_camera_driver_resources_t *resources,
    const esp32_mquickjs_camera_driver_resource_ops_t *ops);
