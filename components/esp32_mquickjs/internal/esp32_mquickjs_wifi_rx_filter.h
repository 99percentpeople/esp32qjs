#pragma once

#include "esp32_mquickjs_wifi_rx_target.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO

#define ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY 8U
#define ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE_MASK 0x0fU

typedef struct {
    uint8_t count;
    uint8_t values[ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY][6];
} esp32_mquickjs_wifi_rx_mac_filter_t;

/* Immutable after admission. Zero type/subtype masks select no packets when
 * enabled; zero MAC count means that address role is unconstrained. */
typedef struct {
    uint8_t type_mask;
    bool subtype_filter, minimum_rssi_set, valid_only;
    uint16_t subtype_mask;
    bool frame_filter;
    uint16_t frame_subtype_masks[4]; /* indexed by MAC type, never SDK category */
    int8_t minimum_rssi;
    esp32_mquickjs_wifi_rx_mac_filter_t source, destination, bssid;
    uint32_t sample_every, maximum_rate_hz; /* rate 0 disables rate limiting */
} esp32_mquickjs_wifi_rx_filter_t;

/* Per subscriber, zero initialized on a new capture run. Caller serializes
 * evaluation/reset; never share this mutable state between subscribers. */
typedef struct {
    uint32_t sample_phase;
    bool last_admitted_set;
    uint64_t last_admitted_us;
} esp32_mquickjs_wifi_rx_filter_state_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,
    ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG,
    ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CALLBACK,
    ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_HEADER,
    ESP32_MQUICKJS_WIFI_RX_FILTER_RX_ERROR,
    ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE,
    ESP32_MQUICKJS_WIFI_RX_FILTER_SUBTYPE,
    ESP32_MQUICKJS_WIFI_RX_FILTER_MAC,
    ESP32_MQUICKJS_WIFI_RX_FILTER_RSSI,
    ESP32_MQUICKJS_WIFI_RX_FILTER_DECIMATION,
    ESP32_MQUICKJS_WIFI_RX_FILTER_RATE,
} esp32_mquickjs_wifi_rx_filter_result_t;

bool esp32_mquickjs_wifi_rx_filter_valid(const esp32_mquickjs_wifi_rx_filter_t *filter);

/* Uses only copied metadata/header, never dereferences view.bytes. now_us must
 * be callback-time monotonic microseconds (not the driver's wrapping RX clock).
 * A rate-admitted candidate consumes the interval even if a later pool/queue
 * allocation fails. Failed static predicates do not consume decimation/rate. */
esp32_mquickjs_wifi_rx_filter_result_t esp32_mquickjs_wifi_rx_filter_evaluate(
    const esp32_mquickjs_wifi_rx_filter_t *filter,
    esp32_mquickjs_wifi_rx_filter_state_t *state,
    const esp32_mquickjs_wifi_rx_target_view_t *view, uint64_t now_us);
#endif
