#pragma once

#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include "esp_wifi.h"
#include "esp32_mquickjs_wifi_rx.h"

typedef enum {
    ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET,
    ESP32_MQUICKJS_WIFI_RX_SPAN_METADATA_ONLY,
    ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_ARGUMENT,
    ESP32_MQUICKJS_WIFI_RX_SPAN_UNKNOWN_TYPE,
    ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_LENGTH,
} esp32_mquickjs_wifi_rx_span_status_t;

/* Pointer-free driver facts. Optional PHY interpretation belongs to the common
 * metadata decoder, not to guessed bitfield layout or a CSI-only dependency. */
typedef struct {
    esp32_mquickjs_wifi_packet_type_t type;
    bool available, he_layout, dump_length_available;
    int8_t rssi, noise_floor;
    uint8_t primary, secondary_raw, rx_state, rx_end_state;
    uint8_t raw_format, raw_rate;
    uint32_t timestamp_us, signal_word1;
    uint16_t signal_word2, driver_length, dump_length;
    bool antenna_available;
    uint8_t antenna, mcs, bandwidth_mhz, ampdu_count;
    /* Common HT facts; AMPDU count is supplied only by the non-HE layout. */
    bool ht_fields_available, stbc, short_gi, ldpc, aggregation, smoothing, sounding;
} esp32_mquickjs_wifi_rx_driver_metadata_t;

/* Borrowed view valid ONLY inside the SDK callback. Never enqueue this struct
 * with its pointer: copy metadata/header plus bounded packet bytes first. */
typedef struct {
    esp32_mquickjs_wifi_rx_span_status_t status;
    esp32_mquickjs_wifi_rx_driver_metadata_t metadata;
    esp32_mquickjs_wifi_rx_header_t header;
    const uint8_t *bytes;
    uint16_t readable_length;
    bool header_type_matches;
} esp32_mquickjs_wifi_rx_target_view_t;

/* Only accepts buffers delivered by the local ESP-IDF promiscuous callback,
 * whose known type guarantees the metadata prefix and documented payload.
 * It is not a validator for arbitrary/untrusted pointers or CSI hdr pointers. */
esp32_mquickjs_wifi_rx_span_status_t esp32_mquickjs_wifi_rx_target_view(
    const void *buffer, wifi_promiscuous_pkt_type_t type,
    esp32_mquickjs_wifi_rx_target_view_t *output);
#endif
