#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_i2s_timer_create_fn)(
    void *opaque, void **out_timer);
typedef void (*esp32_mquickjs_i2s_timer_delete_fn)(
    void *timer, void *opaque);

typedef struct {
    esp32_mquickjs_i2s_timer_create_fn create_rx;
    esp32_mquickjs_i2s_timer_create_fn create_tx;
    esp32_mquickjs_i2s_timer_delete_fn delete_timer;
    void *opaque;
} esp32_mquickjs_i2s_timer_resource_ops_t;

typedef struct {
    void *rx_timer;
    void *tx_timer;
} esp32_mquickjs_i2s_timer_resources_t;

int esp32_mquickjs_i2s_timer_resources_init(
    esp32_mquickjs_i2s_timer_resources_t *resources,
    const esp32_mquickjs_i2s_timer_resource_ops_t *ops,
    bool need_rx, bool need_tx);

void esp32_mquickjs_i2s_timer_resources_deinit(
    esp32_mquickjs_i2s_timer_resources_t *resources,
    const esp32_mquickjs_i2s_timer_resource_ops_t *ops);
