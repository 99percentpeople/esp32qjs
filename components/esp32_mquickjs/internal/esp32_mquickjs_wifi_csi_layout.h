#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Shared CSI value types; no SDK, allocator or pool dependency. */
#define ESP32_MQUICKJS_WIFI_CSI_MAX_SEGMENTS 3U
#define ESP32_MQUICKJS_WIFI_CSI_MAX_SUBCARRIER_RANGES 2U
#define ESP32_MQUICKJS_WIFI_CSI_MAX_NULL_SUBCARRIERS 3U

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY = 0,
    ESP32_MQUICKJS_WIFI_CSI_PHY_HT,
    ESP32_MQUICKJS_WIFI_CSI_PHY_VHT,
    ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU,
    ESP32_MQUICKJS_WIFI_CSI_PHY_HE_MU,
    ESP32_MQUICKJS_WIFI_CSI_PHY_HE_ER_SU,
    ESP32_MQUICKJS_WIFI_CSI_PHY_HE_TB,
    ESP32_MQUICKJS_WIFI_CSI_PHY_UNKNOWN,
} esp32_mquickjs_wifi_csi_phy_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_SECONDARY_NONE = 0,
    ESP32_MQUICKJS_WIFI_CSI_SECONDARY_ABOVE,
    ESP32_MQUICKJS_WIFI_CSI_SECONDARY_BELOW,
} esp32_mquickjs_wifi_csi_secondary_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_UNKNOWN = 0,
    ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8,
    ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT12_LE,
    ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT12_PACKED,
} esp32_mquickjs_wifi_csi_sample_encoding_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_SEGMENT_UNKNOWN = 0,
    ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF,
    ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HT_LTF,
    ESP32_MQUICKJS_WIFI_CSI_SEGMENT_STBC_HT_LTF2,
    ESP32_MQUICKJS_WIFI_CSI_SEGMENT_VHT_LTF,
    ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HE_LTF1,
    ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HE_LTF2,
    ESP32_MQUICKJS_WIFI_CSI_SEGMENT_MIXED,
} esp32_mquickjs_wifi_csi_segment_type_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_UNKNOWN = 0,
    ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_LEGACY,
    ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_HE,
} esp32_mquickjs_wifi_csi_layout_schema_t;

typedef struct {
    int16_t start;
    int16_t end;
} esp32_mquickjs_wifi_csi_subcarrier_range_t;

typedef struct {
    esp32_mquickjs_wifi_csi_segment_type_t type;
    uint32_t offset_bytes;
    uint32_t length_bytes;
    uint32_t iq_pair_count;
    uint8_t subcarrier_range_count;
    esp32_mquickjs_wifi_csi_subcarrier_range_t
        subcarrier_ranges[ESP32_MQUICKJS_WIFI_CSI_MAX_SUBCARRIER_RANGES];
    uint8_t null_subcarrier_count;
    int16_t null_subcarriers[ESP32_MQUICKJS_WIFI_CSI_MAX_NULL_SUBCARRIERS];
} esp32_mquickjs_wifi_csi_segment_t;

typedef struct {
    esp32_mquickjs_wifi_csi_layout_schema_t schema;
    esp32_mquickjs_wifi_csi_sample_encoding_t sample_encoding;
    uint8_t sample_bits;
    uint32_t byte_length;
    uint32_t iq_pair_count;
    uint16_t trailing_padding_bytes;
    uint8_t segment_count;
    bool known;
    esp32_mquickjs_wifi_csi_segment_t
        segments[ESP32_MQUICKJS_WIFI_CSI_MAX_SEGMENTS];
} esp32_mquickjs_wifi_csi_layout_t;
