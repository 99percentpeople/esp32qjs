#pragma once
#include "esp32_mquickjs_wifi_rx_wire.h"
#include "esp32_mquickjs_wifi_csi_layout.h"

/* Bit positions of the normalized sole-v1 RX flags, not SDK bitfields. */
enum {
    ESP32_MQUICKJS_WIFI_RX_WIRE_NOISE_AVAILABLE = 1U << 0,
    ESP32_MQUICKJS_WIFI_RX_WIRE_ANTENNA_AVAILABLE = 1U << 1,
    ESP32_MQUICKJS_WIFI_RX_WIRE_MCS_AVAILABLE = 1U << 2,
    ESP32_MQUICKJS_WIFI_RX_WIRE_BANDWIDTH_AVAILABLE = 1U << 3,
    ESP32_MQUICKJS_WIFI_RX_WIRE_STBC_AVAILABLE = 1U << 4,
    ESP32_MQUICKJS_WIFI_RX_WIRE_STBC = 1U << 5,
    ESP32_MQUICKJS_WIFI_RX_WIRE_SMOOTHING = 1U << 6,
    ESP32_MQUICKJS_WIFI_RX_WIRE_SOUNDING_AVAILABLE = 1U << 7,
    ESP32_MQUICKJS_WIFI_RX_WIRE_SOUNDING = 1U << 8,
    ESP32_MQUICKJS_WIFI_RX_WIRE_AGGREGATION_AVAILABLE = 1U << 9,
    ESP32_MQUICKJS_WIFI_RX_WIRE_AGGREGATION = 1U << 10,
    ESP32_MQUICKJS_WIFI_RX_WIRE_FEC_AVAILABLE = 1U << 11,
    ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC = 1U << 12,
    ESP32_MQUICKJS_WIFI_RX_WIRE_SGI_AVAILABLE = 1U << 13,
    ESP32_MQUICKJS_WIFI_RX_WIRE_SGI = 1U << 14,
    ESP32_MQUICKJS_WIFI_RX_WIRE_ESTIMATE_AVAILABLE = 1U << 15,
    ESP32_MQUICKJS_WIFI_RX_WIRE_ESTIMATE_VALID = 1U << 16,
    ESP32_MQUICKJS_WIFI_RX_WIRE_SMOOTHING_AVAILABLE = 1U << 17,
    ESP32_MQUICKJS_WIFI_RX_WIRE_DCM_AVAILABLE = 1U << 18,
    ESP32_MQUICKJS_WIFI_RX_WIRE_DCM = 1U << 19,
};
/* Values have already been normalized by the target adapter. In particular,
 * PHY unknown is 255, NOT the native CSI enum's UNKNOWN value (7). Timestamp
 * accuracy 2 means callback time; only proven clock mappings may use 0 or 1. */
typedef struct {
    uint64_t timestamp_us;
    uint32_t rx_sequence, session_generation, radio_generation;
    uint8_t addresses[5][6], address_mask;
    int8_t rssi, noise_floor;
    uint8_t primary, secondary, antenna, phy, bandwidth_mhz, mcs;
    uint32_t rx_flags;
    const esp32_mquickjs_wifi_csi_layout_t *csi_layout; /* NULL if no CSI section. */
    bool csi_data_valid, first_word_invalid;
    uint8_t timestamp_accuracy;
    bool frame_control_available, duration_available, sequence_available, qos_available;
    uint16_t frame_control, duration_id, sequence_control, qos_control, header_length;
    uint8_t packet_type, fcs_state, capture_mode;
    bool driver_payload_length_available, driver_packet_length_available;
    uint32_t driver_packet_length;
    uint8_t legacy_rate, signal_mode, ampdu_count, rx_state;
    uint16_t guard_interval_ns; /* 0 unavailable */
    uint8_t he_ltf_size; /* 0 unavailable, 1/2/4 size multiplier */
} esp32_mquickjs_wifi_rx_wire_metadata_t;

/* Zero facts, unavailable optional scalar sentinels, unknown PHY/secondary,
 * unknown RX sequence and callback-time accuracy. No identity is invented. */
void esp32_mquickjs_wifi_rx_wire_metadata_init(esp32_mquickjs_wifi_rx_wire_metadata_t *metadata);
/* Shared preflight for snapshot adapters and the encoder; no output or allocation. */
bool esp32_mquickjs_wifi_rx_wire_metadata_valid(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frame,
    const esp32_mquickjs_wifi_rx_wire_metadata_t *metadata);
/* Inputs (including csi_layout) must remain stable during this call. Reuses the
 * exact frame descriptor used by write_control; duplicate lengths/flags/sequence
 * are derived from it. Unavailable scalar/address bytes are canonicalized, while
 * invalid enum/value-availability/layout/length relationships are rejected.
 * A bounded local record makes output atomic and allows overlap with inputs.
 * No output bytes change on failure; no payload, SDK or JS pointers are read. */
bool esp32_mquickjs_wifi_rx_wire_write_metadata(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frame,
    const esp32_mquickjs_wifi_rx_wire_metadata_t *metadata,
    uint8_t *output, size_t output_capacity);
