#pragma once
#include "esp32_mquickjs_wifi_rx_filter.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include "esp32_mquickjs_native_lease.h"
#include "freertos/FreeRTOS.h"

#define ESP32_MQUICKJS_WIFI_MONITOR_MAX_SNAP_LENGTH 16384U
#define ESP32_MQUICKJS_WIFI_MONITOR_FILTER_COUNTERS (ESP32_MQUICKJS_WIFI_RX_FILTER_RATE + 1U)

typedef struct {
    uint32_t generation, identity;
    uint16_t index;
} esp32_mquickjs_wifi_monitor_event_t;
typedef struct {
    esp32_mquickjs_wifi_rx_driver_metadata_t driver;
    esp32_mquickjs_wifi_rx_header_t header;
    uint64_t callback_time_us;
    uint16_t readable_length, captured_length;
    bool metadata_only, truncated, header_type_matches;
} esp32_mquickjs_wifi_monitor_info_t;
typedef struct {
    esp32_mquickjs_native_lease_t lease;
    esp32_mquickjs_wifi_monitor_info_t info;
    uint8_t owner;
} esp32_mquickjs_wifi_monitor_slot_t;
typedef struct {
    uint64_t callbacks, accepted, dropped_closing, dropped_pool_full, dropped_queue_full;
    uint64_t invalid_callback_data, dropped_required_complete, truncated_frames;
    uint64_t received_bytes, captured_bytes, dropped_identity_exhausted;
    uint64_t filtered[ESP32_MQUICKJS_WIFI_MONITOR_FILTER_COUNTERS];
    uint32_t leased_frames, publishers;
} esp32_mquickjs_wifi_monitor_counters_t;
typedef struct {
    void *(*calloc_fn)(size_t count, size_t size, void *opaque);
    void *(*malloc_fn)(size_t size, void *opaque);
    void (*free_fn)(void *pointer, void *opaque);
    void *opaque;
} esp32_mquickjs_wifi_monitor_allocator_t;
/* Queue bridge: use EventQueue DROP_NEWEST/try_send_from_callback. Success
 * transfers the event root to the queue. False must retain no event/root. It
 * may synchronously deliver/discard on success; publisher never reuses slot
 * pointers after calling this hook. Never block/allocate in the hook. */
typedef bool (*esp32_mquickjs_wifi_monitor_publish_fn)(
    const esp32_mquickjs_wifi_monitor_event_t *event, void *opaque);
typedef struct {
    portMUX_TYPE lock;
    bool lock_initialized, initialized, accepting, require_complete;
    uint32_t generation, next_identity;
    uint16_t capacity, snap_length;
    esp32_mquickjs_native_pool_t pool;
    esp32_mquickjs_wifi_monitor_slot_t *slots;
    uint8_t *payload;
    esp32_mquickjs_wifi_monitor_allocator_t allocator;
    esp32_mquickjs_wifi_monitor_publish_fn publish;
    void *publish_opaque;
    esp32_mquickjs_wifi_monitor_counters_t counters;
} esp32_mquickjs_wifi_monitor_resources_t;
/* Linear retained handle: zero initialize, never bit-copy to create an owner.
 * Frame roots use event tokens + the PUBLIC owner transition instead. */
typedef struct {
    esp32_mquickjs_wifi_monitor_resources_t *resources;
    esp32_mquickjs_wifi_monitor_event_t event;
} esp32_mquickjs_wifi_monitor_ref_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED,
    ESP32_MQUICKJS_WIFI_MONITOR_FILTERED,
    ESP32_MQUICKJS_WIFI_MONITOR_CLOSING,
    ESP32_MQUICKJS_WIFI_MONITOR_INVALID,
    ESP32_MQUICKJS_WIFI_MONITOR_REQUIRED_COMPLETE,
    ESP32_MQUICKJS_WIFI_MONITOR_POOL_FULL,
    ESP32_MQUICKJS_WIFI_MONITOR_QUEUE_FULL,
    ESP32_MQUICKJS_WIFI_MONITOR_IDENTITY_EXHAUSTED,
} esp32_mquickjs_wifi_monitor_publish_result_t;
typedef struct {
    bool initialized, accepting, identity_exhausted;
    uint32_t generation, free_slots;
    size_t allocated_bytes;
    esp32_mquickjs_wifi_monitor_counters_t counters;
} esp32_mquickjs_wifi_monitor_snapshot_t;

/* Control is zero-initialized before first init. First init/control destruction
 * must be externally serialized. Each successful reinit requires a larger
 * generation; never reinitialize a live/retained pool. Allocations/frees occur
 * outside critical sections. No queue or JS roots are allocated by this layer. */
bool esp32_mquickjs_wifi_monitor_resources_size(uint32_t capacity, uint32_t snap_length, size_t *output);
bool esp32_mquickjs_wifi_monitor_resources_init(esp32_mquickjs_wifi_monitor_resources_t *resources,
    uint32_t generation, uint32_t capacity, uint32_t snap_length, bool require_complete,
    const esp32_mquickjs_wifi_monitor_allocator_t *allocator,
    esp32_mquickjs_wifi_monitor_publish_fn publish, void *publish_opaque);
bool esp32_mquickjs_wifi_monitor_resources_set_accepting(esp32_mquickjs_wifi_monitor_resources_t *resources, bool accepting);
/* Caller stops/detaches the Radio subscriber and drains EventQueue first.
 * The control object/context must outlive all frame/retained refs. */
bool esp32_mquickjs_wifi_monitor_resources_deinit(esp32_mquickjs_wifi_monitor_resources_t *resources);
void esp32_mquickjs_wifi_monitor_resources_reset_counters(esp32_mquickjs_wifi_monitor_resources_t *resources);
void esp32_mquickjs_wifi_monitor_resources_snapshot(esp32_mquickjs_wifi_monitor_resources_t *resources,
    esp32_mquickjs_wifi_monitor_snapshot_t *output);
esp32_mquickjs_wifi_monitor_publish_result_t esp32_mquickjs_wifi_monitor_publish(
    esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_rx_target_view_t *view,
    esp32_mquickjs_wifi_rx_filter_result_t filter_result, uint64_t callback_time_us);
/* Compatible with Radio subscribe's sink callback. */
void esp32_mquickjs_wifi_monitor_sink(void *opaque, const esp32_mquickjs_wifi_rx_target_view_t *view,
    esp32_mquickjs_wifi_rx_filter_result_t result, uint64_t callback_time_us);
bool esp32_mquickjs_wifi_monitor_take_event(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event);
bool esp32_mquickjs_wifi_monitor_discard_event(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event);
bool esp32_mquickjs_wifi_monitor_close_frame(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event);
bool esp32_mquickjs_wifi_monitor_frame_info(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event, esp32_mquickjs_wifi_monitor_info_t *output);
bool esp32_mquickjs_wifi_monitor_retain_frame(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event, esp32_mquickjs_wifi_monitor_ref_t *output);
/* Borrowed bytes remain valid while this retained handle is open, including
 * after its Frame or Session closes. Does not create an additional reference. */
bool esp32_mquickjs_wifi_monitor_ref_data(const esp32_mquickjs_wifi_monitor_ref_t *ref,
    const uint8_t **bytes, size_t *length);
bool esp32_mquickjs_wifi_monitor_release_ref(esp32_mquickjs_wifi_monitor_ref_t *ref);
#endif
