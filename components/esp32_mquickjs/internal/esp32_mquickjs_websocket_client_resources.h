#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_websocket_client_resource_fn)(
    void *client, void *opaque);

typedef struct {
    esp32_mquickjs_websocket_client_resource_fn stop;
    esp32_mquickjs_websocket_client_resource_fn unregister_events;
    esp32_mquickjs_websocket_client_resource_fn destroy;
    void *opaque;
} esp32_mquickjs_websocket_client_resource_ops_t;

typedef struct {
    void *client;
    bool started;
    bool events_registered;
} esp32_mquickjs_websocket_client_resources_t;

int esp32_mquickjs_websocket_client_resources_deinit(
    esp32_mquickjs_websocket_client_resources_t *resources,
    const esp32_mquickjs_websocket_client_resource_ops_t *ops);
