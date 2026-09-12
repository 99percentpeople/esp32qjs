#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sole development v1 envelope. Internal foundation, not an advertised Source
 * format until its metadata encoder, stream adapters and Host parser agree. */
#define ESP32_MQUICKJS_WIFI_RX_WIRE_HEADER_BYTES 32U
#define ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES 256U
#define ESP32_MQUICKJS_WIFI_RX_WIRE_SEGMENT_BYTES 40U
#define ESP32_MQUICKJS_WIFI_RX_WIRE_SEGMENT_BASE 128U
#define ESP32_MQUICKJS_WIFI_RX_WIRE_MAX_FRAMES 128U
#define ESP32_MQUICKJS_WIFI_RX_WIRE_CSI_DIRECTORY_BYTES 40U
#define ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR_DIRECTORY_BYTES 24U
#define ESP32_MQUICKJS_WIFI_RX_WIRE_CANONICAL 1U

typedef enum {
    ESP32_MQUICKJS_WIFI_RX_WIRE_CSI = 1,
    ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR = 2,
} esp32_mquickjs_wifi_rx_wire_kind_t;
enum {
    ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PRESENT = 1U << 0,
    ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_TRUNCATED = 1U << 1,
    ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_HEADER_ONLY = 1U << 2,
    ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_PARSED = 1U << 3,
    ESP32_MQUICKJS_WIFI_RX_WIRE_PACKET_POINTER_VALID = 1U << 4,
    ESP32_MQUICKJS_WIFI_RX_WIRE_RECORD_FLAGS = 0x1f,
};
typedef struct {
    uint32_t csi_length;
    uint32_t packet_length; /* Bytes actually held by the retained payload owner. */
    uint32_t driver_payload_length; /* Reported fact, never an allocation/read bound. */
    uint32_t packet_readable_length; /* Adapter-proven source span, not raw sig_len. */
    uint32_t sequence;
    uint16_t captured_header_length; /* Copied header prefix; may be incomplete. */
    uint16_t flags;
} esp32_mquickjs_wifi_rx_wire_frame_t;
typedef struct {
    uint32_t frame_count, directory_bytes, metadata_base, control_bytes, total_bytes;
} esp32_mquickjs_wifi_rx_wire_layout_t;
typedef struct {
    uint32_t metadata_offset, csi_offset, packet_offset, end_offset;
} esp32_mquickjs_wifi_rx_wire_offsets_t;

/* Preflight uses checked arithmetic and commits output only on complete success.
 * Arrays are caller-owned stable snapshots, never concurrently modified. Payload
 * bytes remain in their retained owners; no large frame or batch copies here. */
bool esp32_mquickjs_wifi_rx_wire_layout(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frames, uint32_t frame_count,
    esp32_mquickjs_wifi_rx_wire_layout_t *output);
bool esp32_mquickjs_wifi_rx_wire_offsets(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frames, uint32_t frame_count, uint32_t index,
    esp32_mquickjs_wifi_rx_wire_offsets_t *output);
/* Writes header/directories and zeroes metadata slots. A metadata encoder MUST
 * subsequently fill all records; zeroed slots alone are not valid RX metadata.
 * No bytes are changed on invalid input/size/overlap. Bytes after control remain
 * untouched; stream adapters emit payload spans and explicit zero padding. */
bool esp32_mquickjs_wifi_rx_wire_write_control(esp32_mquickjs_wifi_rx_wire_kind_t kind,
    const esp32_mquickjs_wifi_rx_wire_frame_t *frames, uint32_t frame_count,
    uint8_t *output, size_t output_capacity);
