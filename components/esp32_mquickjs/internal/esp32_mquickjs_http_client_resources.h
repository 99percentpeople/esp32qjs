#pragma once

typedef int (*esp32_mquickjs_http_client_cleanup_fn)(
    void *client, void *opaque);

typedef struct {
    esp32_mquickjs_http_client_cleanup_fn cleanup;
    void *opaque;
} esp32_mquickjs_http_client_resource_ops_t;

typedef struct {
    void *client;
} esp32_mquickjs_http_client_resources_t;

int esp32_mquickjs_http_client_resources_deinit(
    esp32_mquickjs_http_client_resources_t *resources,
    const esp32_mquickjs_http_client_resource_ops_t *ops);
