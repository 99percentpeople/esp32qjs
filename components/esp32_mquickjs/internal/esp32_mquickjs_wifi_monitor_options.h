#pragma once
#include "esp32_mquickjs_wifi_monitor_capture.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#define ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_POOL_CAPACITY 16U
#define ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_QUEUE_CAPACITY 16U
#define ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_SNAP_LENGTH 2048U
typedef struct {
    esp32_mquickjs_wifi_monitor_capture_options_t capture;
    uint32_t pool_capacity, queue_capacity, snap_length;
    bool require_complete;
} esp32_mquickjs_wifi_monitor_options_t;

/* undefined selects defaults; null/unknown fields/coercions are rejected.
 * Output changes only on success. No Radio/driver/native resource allocation;
 * regulatory admission still belongs to the later Radio transaction. */
bool esp32_mquickjs_wifi_monitor_capture_options(JSContext *ctx, JSValue value,
    esp32_mquickjs_wifi_monitor_options_t *output);
#define ESP32_MQUICKJS_WIFI_MONITOR_MAX_BATCH_FRAMES 128U
#define ESP32_MQUICKJS_WIFI_MONITOR_DEFAULT_BATCH_FRAMES 32U
typedef struct {
    uint32_t maximum_frames, minimum_frames, timeout_ms, maximum_latency_ms;
    bool timeout_set;
} esp32_mquickjs_wifi_monitor_batch_options_t;
/* Complete validation before consuming any packet. Maximum is also bounded by
 * this Session's pool capacity; undefined uses defaults, null is rejected. */
bool esp32_mquickjs_wifi_monitor_capture_batch_options(JSContext *ctx, JSValue value,
    uint32_t pool_capacity, esp32_mquickjs_wifi_monitor_batch_options_t *output);
#endif
