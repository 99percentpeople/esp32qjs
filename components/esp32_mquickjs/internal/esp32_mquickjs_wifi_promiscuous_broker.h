#pragma once

#include "esp32_mquickjs_wifi_rx_filter.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO

#define ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS 8U

typedef struct { uint32_t identity; } esp32_mquickjs_wifi_promiscuous_token_t;

/* Called outside registry critical sections, in the native RX dispatch task.
 * Copy bounded bytes before return. No JS/SDK/Radio calls, allocation, logging,
 * waiting or unbounded work. Rejected views are supplied for native counters. */
typedef void (*esp32_mquickjs_wifi_promiscuous_sink_t)(void *context,
    const esp32_mquickjs_wifi_rx_target_view_t *view,
    esp32_mquickjs_wifi_rx_filter_result_t result, uint64_t callback_time_us);

/* Caller-owned control storage. Keep it and context alive and unchanged from
 * successful reserve through successful finish_close, including failed SDK
 * cleanup and runtime teardown. Broker never owns/frees a JS root or context. */
typedef struct {
    esp32_mquickjs_wifi_rx_filter_t filter;
    esp32_mquickjs_wifi_rx_filter_state_t filter_state;
    esp32_mquickjs_wifi_promiscuous_sink_t sink;
    void *context;
} esp32_mquickjs_wifi_promiscuous_subscriber_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK,
    ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT,
    ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_TOKEN,
    ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE,
    ESP32_MQUICKJS_WIFI_PROMISCUOUS_CAPACITY,
    ESP32_MQUICKJS_WIFI_PROMISCUOUS_IDENTITY_EXHAUSTED,
    ESP32_MQUICKJS_WIFI_PROMISCUOUS_DRAINING,
} esp32_mquickjs_wifi_promiscuous_result_t;

typedef struct {
    uint8_t reserved, active, closing;
    bool dispatch_busy, identity_exhausted;
    uint32_t entered, overlapping_dispatches;
    /* Union of reserved + active requirements, excluding closing subscribers.
     * These are common types/subtypes, NOT ESP-IDF driver mask bit positions. */
    uint8_t required_types;
    uint16_t required_control_subtypes;
    bool require_error_frames;
} esp32_mquickjs_wifi_promiscuous_snapshot_t;

/* Internal registry layer, not a Radio/driver ownership grant. Parent driver
 * transaction serializes reserve -> filter update -> activate and begin_close
 * -> filter restoration -> drain/finish_close, keeping its Radio lease until
 * all driver cleanup succeeds. reserve requires a zero output token; failures
 * leave it and subscriber storage unchanged. Never wait for dispatch while holding a lock
 * that a sink can acquire. No SDK callback is installed by these helpers. */
esp32_mquickjs_wifi_promiscuous_result_t esp32_mquickjs_wifi_promiscuous_reserve(
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber,
    const esp32_mquickjs_wifi_rx_filter_t *filter,
    esp32_mquickjs_wifi_promiscuous_sink_t sink, void *context,
    esp32_mquickjs_wifi_promiscuous_token_t *output);
esp32_mquickjs_wifi_promiscuous_result_t esp32_mquickjs_wifi_promiscuous_activate(
    const esp32_mquickjs_wifi_promiscuous_token_t *token);
esp32_mquickjs_wifi_promiscuous_result_t esp32_mquickjs_wifi_promiscuous_begin_close(
    const esp32_mquickjs_wifi_promiscuous_token_t *token);
esp32_mquickjs_wifi_promiscuous_result_t esp32_mquickjs_wifi_promiscuous_finish_close(
    esp32_mquickjs_wifi_promiscuous_token_t *token);
void esp32_mquickjs_wifi_promiscuous_snapshot(esp32_mquickjs_wifi_promiscuous_snapshot_t *output);
/* Atomically validate the exact reserved/active token set before obtaining
 * requirements. Closing slots need not be listed. A count match alone is not
 * ownership proof. Failure clears output and grants no driver mutation. */
bool esp32_mquickjs_wifi_promiscuous_owned_requirements(
    const esp32_mquickjs_wifi_promiscuous_token_t *tokens, unsigned count,
    esp32_mquickjs_wifi_promiscuous_snapshot_t *output);

/* Stable boot-owned dispatcher. Native callback provides a monotonic u64 time.
 * Concurrent/reentrant dispatch is dropped and counted, never waits in RX or
 * concurrently mutates a subscriber's filter phase. Returns false when busy. */
bool esp32_mquickjs_wifi_promiscuous_dispatch(const void *buffer,
    wifi_promiscuous_pkt_type_t type, uint64_t callback_time_us);
#endif
