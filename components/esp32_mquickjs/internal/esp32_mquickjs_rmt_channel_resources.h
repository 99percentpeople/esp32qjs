#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_rmt_channel_create_fn)(
    void *opaque, void **out_channel);
typedef int (*esp32_mquickjs_rmt_encoder_create_fn)(
    void *opaque, void **out_encoder);
typedef int (*esp32_mquickjs_rmt_channel_register_callbacks_fn)(
    void *channel, void *opaque);
typedef int (*esp32_mquickjs_rmt_channel_disable_fn)(
    void *channel, void *opaque);
typedef int (*esp32_mquickjs_rmt_encoder_delete_fn)(
    void *encoder, void *opaque);
typedef int (*esp32_mquickjs_rmt_channel_delete_fn)(
    void *channel, void *opaque);

typedef struct {
    esp32_mquickjs_rmt_channel_create_fn create_channel;
    esp32_mquickjs_rmt_encoder_create_fn create_encoder;
    esp32_mquickjs_rmt_channel_register_callbacks_fn register_callbacks;
    esp32_mquickjs_rmt_channel_disable_fn disable_channel;
    esp32_mquickjs_rmt_encoder_delete_fn delete_encoder;
    esp32_mquickjs_rmt_channel_delete_fn delete_channel;
    void *opaque;
} esp32_mquickjs_rmt_channel_resource_ops_t;

typedef struct {
    void *channel;
    void *encoder;
    bool enabled;
} esp32_mquickjs_rmt_channel_resources_t;

int esp32_mquickjs_rmt_channel_resources_init(
    esp32_mquickjs_rmt_channel_resources_t *resources,
    bool create_encoder,
    const esp32_mquickjs_rmt_channel_resource_ops_t *ops);

int esp32_mquickjs_rmt_channel_resources_deinit(
    esp32_mquickjs_rmt_channel_resources_t *resources,
    const esp32_mquickjs_rmt_channel_resource_ops_t *ops);
