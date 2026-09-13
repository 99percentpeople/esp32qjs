#ifndef ESP32_MQUICKJS_WIFI_CSI_RESOURCES_H
#define ESP32_MQUICKJS_WIFI_CSI_RESOURCES_H

#include "esp32_mquickjs_native_lease.h"
#include "esp32_mquickjs_wifi_csi_layout.h"
#include "esp32_mquickjs_wifi_csi_packet.h"
#include "esp32_mquickjs_wifi_rx_wire_metadata.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS 8U

typedef struct {
    /* Logical address roles from a parsed, proven same-callback MAC header. */
    uint8_t addresses[ESP32_MQUICKJS_WIFI_RX_ADDRESS_COUNT][6];
    uint8_t address_mask;
    esp32_mquickjs_wifi_packet_type_t frame_type;
    uint8_t frame_subtype;
    bool frame_subtype_available;
    int8_t rssi;
    int8_t noise_floor;
    uint8_t channel;
    uint8_t antenna;
    uint8_t mcs;
    uint8_t bandwidth_mhz;
    uint16_t guard_interval_ns; /* 0 unavailable; nanoseconds, not timestamp accuracy. */
    esp32_mquickjs_wifi_csi_phy_t phy;
    esp32_mquickjs_wifi_csi_secondary_t secondary;
    uint32_t driver_timestamp_us;
    uint64_t timestamp_us; /* Monotonic time sampled at CSI callback entry. */
    uint32_t radio_generation; /* Physical Radio lease generation, not channel revision. */
    uint32_t rx_sequence;
    uint32_t phy_flags; /* Optional common RX boolean value/availability pairs. */
    uint8_t ampdu_count;
    bool ampdu_count_available;
    bool noise_floor_available;
    bool antenna_available;
    bool mcs_available;
    bool bandwidth_available;
    bool stbc;
    bool stbc_available;
    bool first_word_invalid;
    bool channel_estimate_valid;
    bool channel_estimate_valid_available;
    uint8_t he_ltf_size; /* 0 unavailable; 1x/2x/4x symbol size, not symbol count. */
    uint8_t dcm_state; /* 0 unavailable, 1 disabled, 2 enabled. */
    esp32_mquickjs_wifi_csi_layout_t layout;
} esp32_mquickjs_wifi_csi_metadata_t;

typedef struct {
    uint8_t source_macs[ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS][6];
    uint8_t destination_macs[ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS][6];
    uint8_t bssids[ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS][6];
    uint8_t bssid_count;
    uint8_t frame_types;
    uint16_t frame_subtypes;
    bool frame_types_set, frame_subtypes_set;
    uint16_t frame_subtype_masks[4];
    bool frame_filter;
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
    esp32_mquickjs_wifi_csi_packet_t *packet;
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
    _Atomic uint32_t filtered_bssid;
    _Atomic uint32_t filtered_frame_type;
    _Atomic uint32_t filtered_frame_subtype;
    _Atomic uint32_t dropped_identity_exhausted;
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
    _Atomic uint32_t packet_unavailable;
    _Atomic uint32_t packet_malformed;
    _Atomic uint32_t packet_truncated;
    _Atomic uint32_t dropped_packet_required;
    _Atomic uint32_t dropped_packet_incomplete;
    _Atomic uint32_t received_packet_bytes;
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
    /* Optional accounting notification, called while the store control pin
     * still owns all allocations. No allocation is freed by this hook. */
    esp32_mquickjs_wifi_csi_free_fn retire_fn;
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
    uint32_t max_packet_bytes;
    esp32_mquickjs_wifi_csi_packet_options_t packet_options;
    _Atomic uint32_t callbacks_active;
    _Atomic uint32_t sequence;
    _Atomic bool accepting;
    _Atomic bool identity_exhausted;
    uint32_t filter_phase; /* Cyclic 0..sample_every-1, independent of counters. */
    uint64_t last_accepted_timestamp_us;
    bool last_accepted_timestamp_set;
} esp32_mquickjs_wifi_csi_resources_t;
/* The caller pins resources against final free. Per-counter atomic reset;
 * leases, callback activity, sequence and filter scheduling remain intact. */
void esp32_mquickjs_wifi_csi_resources_reset_counters(esp32_mquickjs_wifi_csi_resources_t *resources);

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
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_PACKET_REQUIRED,
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_PACKET_INCOMPLETE,
    ESP32_MQUICKJS_WIFI_CSI_PUBLISH_IDENTITY_EXHAUSTED,
} esp32_mquickjs_wifi_csi_publish_result_t;

bool esp32_mquickjs_wifi_csi_resources_size(uint32_t capacity, uint32_t max_frame_bytes,
    uint32_t max_packet_bytes, size_t *slots_bytes, size_t *payload_bytes);

bool esp32_mquickjs_wifi_csi_resources_init(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    uint32_t generation,
    uint32_t capacity,
    uint32_t max_frame_bytes,
    uint32_t max_packet_bytes,
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
    const esp32_mquickjs_wifi_csi_packet_input_t *packet_input,
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
