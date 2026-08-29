#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_i2s_channels_create_fn)(
    void *opaque, void **out_tx_channel, void **out_rx_channel);
typedef int (*esp32_mquickjs_i2s_channel_enable_fn)(
    void *channel, void *opaque);
typedef int (*esp32_mquickjs_i2s_channel_disable_fn)(
    void *channel, void *opaque);
typedef int (*esp32_mquickjs_i2s_channel_delete_fn)(
    void *channel, void *opaque);

typedef struct {
    esp32_mquickjs_i2s_channels_create_fn create_channels;
    esp32_mquickjs_i2s_channel_enable_fn enable_channel;
    esp32_mquickjs_i2s_channel_disable_fn disable_channel;
    esp32_mquickjs_i2s_channel_delete_fn delete_channel;
    void *opaque;
} esp32_mquickjs_i2s_channel_resource_ops_t;

typedef struct {
    void *rx_channel;
    void *tx_channel;
    bool rx_enabled;
    bool tx_enabled;
} esp32_mquickjs_i2s_channel_resources_t;

int esp32_mquickjs_i2s_channel_resources_init(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    bool need_rx, bool need_tx,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops);

int esp32_mquickjs_i2s_channel_resources_start(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops);

int esp32_mquickjs_i2s_channel_resources_stop(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops);

int esp32_mquickjs_i2s_channel_resources_delete(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops);

int esp32_mquickjs_i2s_channel_resources_deinit(
    esp32_mquickjs_i2s_channel_resources_t *resources,
    const esp32_mquickjs_i2s_channel_resource_ops_t *ops);
