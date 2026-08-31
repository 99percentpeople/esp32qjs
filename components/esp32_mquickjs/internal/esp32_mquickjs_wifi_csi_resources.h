#ifndef ESP32_MQUICKJS_WIFI_CSI_RESOURCES_H
#define ESP32_MQUICKJS_WIFI_CSI_RESOURCES_H

#include "esp32_mquickjs_native_lease.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS 8U
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

typedef struct {
    uint8_t source_mac[6];
    uint8_t destination_mac[6];
    int8_t rssi;
    int8_t noise_floor;
    uint8_t channel;
    uint8_t antenna;
    uint8_t mcs;
    uint8_t bandwidth_mhz;
    esp32_mquickjs_wifi_csi_phy_t phy;
    esp32_mquickjs_wifi_csi_secondary_t secondary;
    uint32_t driver_timestamp_us;
    uint64_t timestamp_us;
    uint32_t rx_sequence;
    bool destination_mac_available;
    bool noise_floor_available;
    bool antenna_available;
    bool mcs_available;
    bool bandwidth_available;
    bool stbc;
    bool stbc_available;
    bool first_word_invalid;
    bool channel_estimate_valid;
    bool channel_estimate_valid_available;
    esp32_mquickjs_wifi_csi_layout_t layout;
} esp32_mquickjs_wifi_csi_metadata_t;

typedef struct {
    uint8_t source_macs[ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS][6];
    uint8_t destination_macs[ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS][6];
    uint8_t source_mac_count;
    uint8_t destination_mac_count;
    int8_t minimum_rssi;
    uint32_t sample_every;
    uint32_t maximum_rate_hz;
    bool minimum_rssi_set;
    bool valid_only;
} esp32_mquickjs_wifi_csi_filter_t;

typedef struct {
    uint32_t session_generation;
    uint16_t slot_index;
    uint32_t slot_generation;
} esp32_mquickjs_wifi_csi_event_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_OWNER_NONE = 0,
    ESP32_MQUICKJS_WIFI_CSI_OWNER_EVENT,
    ESP32_MQUICKJS_WIFI_CSI_OWNER_PUBLIC,
    ESP32_MQUICKJS_WIFI_CSI_OWNER_CLOSED,
} esp32_mquickjs_wifi_csi_owner_t;

typedef struct {
    uint32_t session_generation;
    uint32_t slot_generation;
    uint32_t sequence;
    esp32_mquickjs_wifi_csi_metadata_t metadata;
    size_t length;
    uint8_t *payload;
    esp32_mquickjs_native_lease_t lease;
    _Atomic uint8_t owner;
    _Atomic bool return_accounted;
} esp32_mquickjs_wifi_csi_slot_t;

typedef struct {
    _Atomic uint32_t callbacks;
    _Atomic uint32_t accepted;
    _Atomic uint32_t delivered_frames;
    _Atomic uint32_t delivered_batches;
    _Atomic uint32_t filtered_mac;
    _Atomic uint32_t filtered_rssi;
    _Atomic uint32_t filtered_decimation;
    _Atomic uint32_t filtered_rate_limit;
    _Atomic uint32_t filtered_first_word_invalid;
    _Atomic uint32_t filtered_channel_estimate_invalid;
    _Atomic uint32_t invalid_callback_data;
    _Atomic uint32_t dropped_pool_full;
    _Atomic uint32_t dropped_queue_full;
    _Atomic uint32_t dropped_frame_too_large;
    _Atomic uint32_t dropped_closing;
    _Atomic uint32_t received_bytes;
    _Atomic uint32_t leased_frames;
} esp32_mquickjs_wifi_csi_counters_t;

typedef void *(*esp32_mquickjs_wifi_csi_calloc_fn)(
    size_t count, size_t size, void *opaque);
typedef void *(*esp32_mquickjs_wifi_csi_malloc_fn)(
    size_t size, void *opaque);
typedef void (*esp32_mquickjs_wifi_csi_free_fn)(void *ptr, void *opaque);

typedef struct {
    esp32_mquickjs_wifi_csi_calloc_fn calloc_fn;
    esp32_mquickjs_wifi_csi_malloc_fn malloc_fn;
    esp32_mquickjs_wifi_csi_free_fn free_fn;
    void *opaque;
} esp32_mquickjs_wifi_csi_allocator_t;

typedef struct {
    esp32_mquickjs_native_pool_t pool;
    esp32_mquickjs_wifi_csi_slot_t *slots;
    uint8_t *payload_storage;
    esp32_mquickjs_wifi_csi_allocator_t allocator;
    esp32_mquickjs_wifi_csi_filter_t filter;
    esp32_mquickjs_wifi_csi_counters_t counters;
    uint32_t generation;
    uint32_t capacity;
    uint32_t max_frame_bytes;
    _Atomic uint32_t callbacks_active;
    _Atomic uint32_t sequence;
    _Atomic bool accepting;
    uint32_t filter_qualified;
    uint64_t last_accepted_timestamp_us;
    bool last_accepted_timestamp_set;
    uint32_t last_driver_timestamp_us;
    uint64_t driver_timestamp_epoch_us;
    bool driver_timestamp_set;
} esp32_mquickjs_wifi_csi_resources_t;

typedef bool (*esp32_mquickjs_wifi_csi_publish_fn)(
    const esp32_mquickjs_wifi_csi_event_t *event, void *opaque);

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED = 0,
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_FILTERED,
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_POOL_FULL,
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_QUEUE_FULL,
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_TOO_LARGE,
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_CLOSING,
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_INVALID,
} esp32_mquickjs_wifi_csi_publish_result_t;

bool esp32_mquickjs_wifi_csi_resources_init(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    uint32_t generation,
    uint32_t capacity,
    uint32_t max_frame_bytes,
    const esp32_mquickjs_wifi_csi_allocator_t *allocator);
bool esp32_mquickjs_wifi_csi_resources_deinit(
    esp32_mquickjs_wifi_csi_resources_t *resources);
void esp32_mquickjs_wifi_csi_resources_set_accepting(
    esp32_mquickjs_wifi_csi_resources_t *resources, bool accepting);
bool esp32_mquickjs_wifi_csi_callback_enter(
    esp32_mquickjs_wifi_csi_resources_t *resources);
void esp32_mquickjs_wifi_csi_callback_leave(
    esp32_mquickjs_wifi_csi_resources_t *resources);
bool esp32_mquickjs_wifi_csi_filter_accept(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_metadata_t *metadata);
esp32_mquickjs_wifi_csi_publish_result_t
esp32_mquickjs_wifi_csi_callback_publish(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_metadata_t *metadata,
    const uint8_t *payload,
    size_t length,
    esp32_mquickjs_wifi_csi_publish_fn publish,
    void *publish_opaque);
esp32_mquickjs_wifi_csi_slot_t *esp32_mquickjs_wifi_csi_slot_from_event(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_event_t *event);
bool esp32_mquickjs_wifi_csi_slot_retain(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    esp32_mquickjs_wifi_csi_slot_t *slot);
bool esp32_mquickjs_wifi_csi_slot_release(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    esp32_mquickjs_wifi_csi_slot_t *slot);
bool esp32_mquickjs_wifi_csi_slot_take_event_owner(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_event_t *event);
bool esp32_mquickjs_wifi_csi_slot_discard_event(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_event_t *event);
bool esp32_mquickjs_wifi_csi_slot_close_public_owner(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    esp32_mquickjs_wifi_csi_slot_t *slot);

#ifdef __cplusplus
}
#endif

#endif
