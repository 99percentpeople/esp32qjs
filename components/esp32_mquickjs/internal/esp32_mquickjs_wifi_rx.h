#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Internal common RX contract. A target adapter must prove the readable span
 * before calling; SDK sig_len by itself is not a buffer allocation length.
 * No SDK/JS pointers, allocations, payload decoding or FCS inference here. */
typedef enum {
    ESP32_MQUICKJS_WIFI_PACKET_MANAGEMENT,
    ESP32_MQUICKJS_WIFI_PACKET_CONTROL,
    ESP32_MQUICKJS_WIFI_PACKET_DATA,
    ESP32_MQUICKJS_WIFI_PACKET_MISC,
    ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN,
} esp32_mquickjs_wifi_packet_type_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_RX_PARSED,
    ESP32_MQUICKJS_WIFI_RX_INVALID_ARGUMENT,
    ESP32_MQUICKJS_WIFI_RX_SHORT_HEADER,
    ESP32_MQUICKJS_WIFI_RX_UNSUPPORTED_VERSION,
    ESP32_MQUICKJS_WIFI_RX_UNSUPPORTED_LAYOUT,
    ESP32_MQUICKJS_WIFI_RX_INVALID_FLAGS,
} esp32_mquickjs_wifi_rx_parse_status_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_RX_SOURCE,
    ESP32_MQUICKJS_WIFI_RX_DESTINATION,
    ESP32_MQUICKJS_WIFI_RX_TRANSMITTER,
    ESP32_MQUICKJS_WIFI_RX_RECEIVER,
    ESP32_MQUICKJS_WIFI_RX_BSSID,
    ESP32_MQUICKJS_WIFI_RX_ADDRESS_COUNT,
} esp32_mquickjs_wifi_rx_address_t;

typedef struct {
    esp32_mquickjs_wifi_rx_parse_status_t status;
    esp32_mquickjs_wifi_packet_type_t type;
    uint8_t version, subtype, address_mask;
    uint8_t addresses[ESP32_MQUICKJS_WIFI_RX_ADDRESS_COUNT][6];
    bool frame_control_valid, duration_valid, sequence_valid, qos_valid, ht_valid;
    uint16_t frame_control, duration_id, sequence_control, qos_control;
    uint32_t ht_control;
    /* Zero for unknown layout; required length also survives SHORT_HEADER.
     * A nonzero length is not proof of a valid/complete header: inspect status. */
    uint8_t header_length;
} esp32_mquickjs_wifi_rx_header_t;

esp32_mquickjs_wifi_rx_parse_status_t esp32_mquickjs_wifi_rx_parse_header(
    const uint8_t *bytes, size_t readable_length, esp32_mquickjs_wifi_rx_header_t *output);
const char *esp32_mquickjs_wifi_rx_subtype_name(
    esp32_mquickjs_wifi_packet_type_t type, uint8_t subtype);
