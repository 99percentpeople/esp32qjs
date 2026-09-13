#pragma once
#include "esp32_mquickjs_wifi_monitor_queue.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include "esp32_mquickjs_wifi_radio.h"

typedef enum {
    ESP32_MQUICKJS_WIFI_MONITOR_STOPPED,
    ESP32_MQUICKJS_WIFI_MONITOR_STARTING,
    ESP32_MQUICKJS_WIFI_MONITOR_RUNNING,
    ESP32_MQUICKJS_WIFI_MONITOR_STOPPING,
    ESP32_MQUICKJS_WIFI_MONITOR_CLOSED,
} esp32_mquickjs_wifi_monitor_capture_state_t;
typedef struct {
    esp32_mquickjs_wifi_rx_filter_t filter;
    uint8_t channel; /* 0 follows Radio; nonzero holds a fixed-channel lease. */
} esp32_mquickjs_wifi_monitor_capture_options_t;
typedef struct {
    bool initialized, channel_claimed;
    esp32_mquickjs_wifi_monitor_capture_state_t state;
    _Atomic bool stop_requested, close_requested, channel_conflicted;
    esp32_mquickjs_wifi_monitor_resources_t *resources;
    esp32_mquickjs_wifi_monitor_queue_t *bridge;
    esp32_mquickjs_wifi_monitor_capture_options_t options;
    esp32_mquickjs_wifi_radio_lease_t radio;
    esp32_mquickjs_wifi_radio_promiscuous_lease_t promiscuous;
    esp32_mquickjs_wifi_promiscuous_subscriber_t subscriber;
    uint8_t effective_channel;
    wifi_second_chan_t effective_secondary;
    uint32_t radio_generation, channel_generation;
    esp_err_t last_error, cleanup_error;
    const char *last_stage, *cleanup_stage;
    void (*notify_stop)(void *opaque);
    void *notify_opaque;
} esp32_mquickjs_wifi_monitor_capture_t;

/* Caller-owned stable control, zero initialized once. All lifecycle calls and
 * state reads are externally serialized; request_stop is callback/reaper safe.
 * Keep control/bridge/resources alive through Radio drain and queue/context/
 * Frame refs. CLOSED releases Radio and producer queue, not retained payloads.
 * This layer never stops/restarts shared Wi-Fi or destroys caller storage. */
esp_err_t esp32_mquickjs_wifi_monitor_capture_init(esp32_mquickjs_wifi_monitor_capture_t *capture,
    esp32_mquickjs_wifi_monitor_resources_t *resources, esp32_mquickjs_wifi_monitor_queue_t *bridge,
    const esp32_mquickjs_wifi_monitor_capture_options_t *options);
esp_err_t esp32_mquickjs_wifi_monitor_capture_start(esp32_mquickjs_wifi_monitor_capture_t *capture);
void esp32_mquickjs_wifi_monitor_capture_request_stop(esp32_mquickjs_wifi_monitor_capture_t *capture, bool close);
/* ESP_ERR_TIMEOUT with no cleanup diagnostic is an entered dispatch draining,
 * not a driver failure. Retry this suffix; do not recreate tokens/storage. */
esp_err_t esp32_mquickjs_wifi_monitor_capture_stop(esp32_mquickjs_wifi_monitor_capture_t *capture);
esp_err_t esp32_mquickjs_wifi_monitor_capture_close(esp32_mquickjs_wifi_monitor_capture_t *capture);
#endif
