#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_ledc_resource_action_fn)(int index, void *opaque);

typedef struct {
    esp32_mquickjs_ledc_resource_action_fn stop_channel;
    esp32_mquickjs_ledc_resource_action_fn deconfigure_channel;
    esp32_mquickjs_ledc_resource_action_fn pause_timer;
    esp32_mquickjs_ledc_resource_action_fn deconfigure_timer;
    void *opaque;
} esp32_mquickjs_ledc_resource_ops_t;

typedef struct {
    bool configured;
    bool stopped;
} esp32_mquickjs_ledc_channel_resources_t;

typedef struct {
    bool configured;
    bool paused;
} esp32_mquickjs_ledc_timer_resources_t;

int esp32_mquickjs_ledc_channel_resources_deinit(
    esp32_mquickjs_ledc_channel_resources_t *resources,
    int channel,
    const esp32_mquickjs_ledc_resource_ops_t *ops);

int esp32_mquickjs_ledc_timer_resources_deinit(
    esp32_mquickjs_ledc_timer_resources_t *resources,
    int timer,
    const esp32_mquickjs_ledc_resource_ops_t *ops);
