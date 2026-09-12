#pragma once
#include "esp32_mquickjs_wifi_rx.h"

#define ESP32_MQUICKJS_WIFI_CSI_MAX_HEADER_BYTES 36U
#define ESP32_MQUICKJS_WIFI_CSI_MAX_PACKET_BYTES 16384U
#define ESP32_MQUICKJS_WIFI_CSI_DEFAULT_PACKET_BYTES 1600U

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_PACKET_NONE,
    ESP32_MQUICKJS_WIFI_CSI_PACKET_HEADER,
    ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL,
} esp32_mquickjs_wifi_csi_packet_mode_t;
typedef struct {
    esp32_mquickjs_wifi_csi_packet_mode_t mode;
    uint32_t snap_length;
    bool required;
    bool require_complete;
} esp32_mquickjs_wifi_csi_packet_options_t;

/* Borrowed only during publication. copied_length comes from the same native
 * constructor's completed-copy receipt. Neither reported length grants reads. */
typedef struct {
    const uint8_t *bytes;
    size_t copied_length;
    uint32_t driver_packet_length;
    uint32_t driver_payload_length;
} esp32_mquickjs_wifi_csi_packet_input_t;
typedef struct {
    esp32_mquickjs_wifi_rx_header_t header;
    uint8_t *bytes;
    uint32_t length;
    uint32_t readable_length;
    uint32_t driver_packet_length;
    uint32_t driver_payload_length;
    esp32_mquickjs_wifi_csi_packet_mode_t mode;
    bool truncated;
} esp32_mquickjs_wifi_csi_packet_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_PACKET_OK,
    ESP32_MQUICKJS_WIFI_CSI_PACKET_UNAVAILABLE,
    ESP32_MQUICKJS_WIFI_CSI_PACKET_MALFORMED,
    ESP32_MQUICKJS_WIFI_CSI_PACKET_INCOMPLETE,
} esp32_mquickjs_wifi_csi_packet_result_t;

bool esp32_mquickjs_wifi_csi_packet_options_valid(
    const esp32_mquickjs_wifi_csi_packet_options_t *options);
uint32_t esp32_mquickjs_wifi_csi_packet_capacity(
    const esp32_mquickjs_wifi_csi_packet_options_t *options);
/* Parses within the proven bound and selects a prefix; no allocation or copy.
 * NONE still inspects the header for filters/metadata, but selects zero bytes.
 * On success output.bytes borrows input until the caller copies to its slot. */
esp32_mquickjs_wifi_csi_packet_result_t esp32_mquickjs_wifi_csi_packet_prepare(
    const esp32_mquickjs_wifi_csi_packet_options_t *options,
    const esp32_mquickjs_wifi_csi_packet_input_t *input,
    esp32_mquickjs_wifi_csi_packet_t *output);
