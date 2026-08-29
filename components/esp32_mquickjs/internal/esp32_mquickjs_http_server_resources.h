#pragma once

typedef int (*esp32_mquickjs_http_server_stop_fn)(void *handle, void *opaque);

typedef struct {
    esp32_mquickjs_http_server_stop_fn stop;
    void *opaque;
} esp32_mquickjs_http_server_resource_ops_t;

typedef struct {
    void *handle;
} esp32_mquickjs_http_server_resources_t;

int esp32_mquickjs_http_server_resources_stop(
    esp32_mquickjs_http_server_resources_t *resources,
    const esp32_mquickjs_http_server_resource_ops_t *ops);
