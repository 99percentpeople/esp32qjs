#include "esp32_mquickjs_wifi_csi_resources.h"

void esp32_mquickjs_wifi_csi_resources_reset_counters(esp32_mquickjs_wifi_csi_resources_t *resources)
{
    if (resources == NULL) return;
#define CSI_RESET(name) atomic_store_explicit(&resources->counters.name, 0, memory_order_relaxed)
    CSI_RESET(callbacks);
    CSI_RESET(accepted);
    CSI_RESET(delivered_frames);
    CSI_RESET(delivered_batches);
    CSI_RESET(filtered_mac);
    CSI_RESET(filtered_bssid);
    CSI_RESET(filtered_frame_type);
    CSI_RESET(filtered_frame_subtype);
    CSI_RESET(dropped_identity_exhausted);
    CSI_RESET(filtered_rssi);
    CSI_RESET(filtered_decimation);
    CSI_RESET(filtered_rate_limit);
    CSI_RESET(filtered_first_word_invalid);
    CSI_RESET(filtered_channel_estimate_invalid);
    CSI_RESET(invalid_callback_data);
    CSI_RESET(dropped_pool_full);
    CSI_RESET(dropped_queue_full);
    CSI_RESET(dropped_frame_too_large);
    CSI_RESET(dropped_closing);
    CSI_RESET(received_bytes);
    CSI_RESET(packet_unavailable);
    CSI_RESET(packet_malformed);
    CSI_RESET(packet_truncated);
    CSI_RESET(dropped_packet_required);
    CSI_RESET(dropped_packet_incomplete);
    CSI_RESET(received_packet_bytes);
#undef CSI_RESET
}

#include <limits.h>
#include <string.h>

static void wifi_csi_resources_clear(
    esp32_mquickjs_wifi_csi_resources_t *resources)
{
    memset(resources, 0, sizeof(*resources));
}

bool esp32_mquickjs_wifi_csi_resources_size(uint32_t capacity, uint32_t max_frame_bytes,
    uint32_t max_packet_bytes, size_t *slots_bytes, size_t *payload_bytes)
{
    if (slots_bytes == NULL || payload_bytes == NULL || capacity == 0 ||
        capacity > ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY || max_frame_bytes == 0 ||
        max_packet_bytes > ESP32_MQUICKJS_WIFI_CSI_MAX_PACKET_BYTES) return false;
    size_t slot_stride = sizeof(esp32_mquickjs_wifi_csi_slot_t) +
        (max_packet_bytes != 0 ? sizeof(esp32_mquickjs_wifi_csi_packet_t) : 0);
    size_t data_stride = (size_t)max_frame_bytes + max_packet_bytes;
    if (data_stride < (size_t)max_frame_bytes ||
        capacity > SIZE_MAX / slot_stride || capacity > SIZE_MAX / data_stride)
        return false;
    *slots_bytes = capacity * slot_stride;
    *payload_bytes = capacity * data_stride;
    return true;
}

bool esp32_mquickjs_wifi_csi_resources_init(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    uint32_t generation,
    uint32_t capacity,
    uint32_t max_frame_bytes,
    uint32_t max_packet_bytes,
    const esp32_mquickjs_wifi_csi_allocator_t *allocator)
{
    uint32_t index;
    size_t slots_bytes, payload_bytes;

    if (resources == NULL || allocator == NULL ||
        allocator->calloc_fn == NULL || allocator->malloc_fn == NULL ||
        allocator->free_fn == NULL || generation == 0U || capacity == 0U ||
        capacity > ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY ||
        !esp32_mquickjs_wifi_csi_resources_size(capacity, max_frame_bytes,
            max_packet_bytes, &slots_bytes, &payload_bytes)) {
        return false;
    }
    wifi_csi_resources_clear(resources);
    resources->allocator = *allocator;
    resources->slots = allocator->calloc_fn(
        1, slots_bytes, allocator->opaque);
    if (resources->slots == NULL) {
        wifi_csi_resources_clear(resources);
        return false;
    }
    resources->payload_storage = allocator->malloc_fn(
        payload_bytes, allocator->opaque);
    if (resources->payload_storage == NULL) {
        allocator->free_fn(resources->slots, allocator->opaque);
        wifi_csi_resources_clear(resources);
        return false;
    }
    if (!esp32_mquickjs_native_pool_init(&resources->pool, capacity)) {
        allocator->free_fn(resources->payload_storage, allocator->opaque);
        allocator->free_fn(resources->slots, allocator->opaque);
        wifi_csi_resources_clear(resources);
        return false;
    }
    resources->generation = generation;
    resources->capacity = capacity;
    resources->max_frame_bytes = max_frame_bytes;
    resources->max_packet_bytes = max_packet_bytes;
    resources->filter.sample_every = 1U;
    resources->filter.valid_only = true;
    _Static_assert(_Alignof(esp32_mquickjs_wifi_csi_slot_t) >=
        _Alignof(esp32_mquickjs_wifi_csi_packet_t), "CSI packet records need aligned slot tail");
    esp32_mquickjs_wifi_csi_packet_t *packets = (void *)(resources->slots + capacity);
    for (index = 0; index < capacity; ++index) {
        resources->slots[index].payload = resources->payload_storage +
            ((size_t)index * ((size_t)max_frame_bytes + max_packet_bytes));
        if (max_packet_bytes != 0) {
            resources->slots[index].packet = &packets[index];
            packets[index].bytes = resources->slots[index].payload + max_frame_bytes;
        }
        atomic_init(&resources->slots[index].return_accounted, true);
        atomic_init(&resources->slots[index].owner,
                    ESP32_MQUICKJS_WIFI_CSI_OWNER_NONE);
    }
    atomic_init(&resources->callbacks_active, 0U);
    atomic_init(&resources->sequence, 0U);
    atomic_init(&resources->accepting, false);
    atomic_init(&resources->identity_exhausted, false);
    return true;
}

bool esp32_mquickjs_wifi_csi_resources_deinit(
    esp32_mquickjs_wifi_csi_resources_t *resources)
{
    esp32_mquickjs_wifi_csi_allocator_t allocator;

    if (resources == NULL || resources->slots == NULL ||
        atomic_load_explicit(&resources->callbacks_active,
                             memory_order_acquire) != 0U ||
        atomic_load_explicit(&resources->counters.leased_frames,
                             memory_order_acquire) != 0U) {
        return false;
    }
    atomic_store_explicit(&resources->accepting, false,
                          memory_order_release);
    allocator = resources->allocator;
    allocator.free_fn(resources->payload_storage, allocator.opaque);
    allocator.free_fn(resources->slots, allocator.opaque);
    wifi_csi_resources_clear(resources);
    return true;
}

void esp32_mquickjs_wifi_csi_resources_set_accepting(
    esp32_mquickjs_wifi_csi_resources_t *resources, bool accepting)
{
    if (resources != NULL) {
        atomic_store_explicit(&resources->accepting, accepting &&
            !atomic_load_explicit(&resources->identity_exhausted, memory_order_acquire),
                              memory_order_release);
    }
}

bool esp32_mquickjs_wifi_csi_callback_enter(
    esp32_mquickjs_wifi_csi_resources_t *resources)
{
    if (resources == NULL) {
        return false;
    }
    atomic_fetch_add_explicit(&resources->callbacks_active, 1U,
                              memory_order_acq_rel);
    atomic_fetch_add_explicit(&resources->counters.callbacks, 1U,
                              memory_order_relaxed);
    if (atomic_load_explicit(&resources->identity_exhausted, memory_order_acquire)) {
        atomic_fetch_add_explicit(&resources->counters.dropped_identity_exhausted, 1, memory_order_relaxed);
        return false;
    }
    if (!atomic_load_explicit(&resources->accepting, memory_order_acquire)) {
        atomic_fetch_add_explicit(&resources->counters.dropped_closing, 1U,
                                  memory_order_relaxed);
        return false;
    }
    return true;
}

void esp32_mquickjs_wifi_csi_callback_leave(
    esp32_mquickjs_wifi_csi_resources_t *resources)
{
    if (resources != NULL) {
        atomic_fetch_sub_explicit(&resources->callbacks_active, 1U,
                                  memory_order_release);
    }
}

static bool wifi_csi_mac_in_list(const uint8_t mac[6],
                                 const uint8_t list[][6],
                                 uint8_t count)
{
    uint8_t index;

    for (index = 0; index < count; ++index) {
        if (memcmp(mac, list[index], 6U) == 0) {
            return true;
        }
    }
    return false;
}

bool esp32_mquickjs_wifi_csi_filter_accept(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_metadata_t *metadata)
{
    esp32_mquickjs_wifi_csi_filter_t *filter;
    uint32_t phase;

    if (resources == NULL || metadata == NULL) {
        return false;
    }
    filter = &resources->filter;
    if (filter->source_mac_count > ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS ||
        filter->destination_mac_count > ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS ||
        filter->bssid_count > ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS ||
        filter->sample_every == 0 || filter->maximum_rate_hz > 1000000U ||
        (filter->frame_types & ~31U) != 0) return false;
    if ((filter->source_mac_count > 0U &&
         (!(metadata->address_mask & (1U << ESP32_MQUICKJS_WIFI_RX_SOURCE)) ||
          !wifi_csi_mac_in_list(metadata->addresses[ESP32_MQUICKJS_WIFI_RX_SOURCE],
                               filter->source_macs, filter->source_mac_count))) ||
        (filter->destination_mac_count > 0U &&
         (!(metadata->address_mask & (1U << ESP32_MQUICKJS_WIFI_RX_DESTINATION)) ||
          !wifi_csi_mac_in_list(metadata->addresses[ESP32_MQUICKJS_WIFI_RX_DESTINATION],
                               filter->destination_macs, filter->destination_mac_count)))) {
        atomic_fetch_add_explicit(&resources->counters.filtered_mac, 1U, memory_order_relaxed);
        return false;
    }
    if (filter->bssid_count > 0 &&
        (!(metadata->address_mask & (1U << ESP32_MQUICKJS_WIFI_RX_BSSID)) ||
         !wifi_csi_mac_in_list(metadata->addresses[ESP32_MQUICKJS_WIFI_RX_BSSID],
                              filter->bssids, filter->bssid_count))) {
        atomic_fetch_add_explicit(&resources->counters.filtered_bssid, 1, memory_order_relaxed);
        return false;
    }
    if (filter->frame_types_set && ((unsigned)metadata->frame_type > ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN ||
        !(filter->frame_types & (1U << metadata->frame_type)))) {
        atomic_fetch_add_explicit(&resources->counters.filtered_frame_type, 1, memory_order_relaxed);
        return false;
    }
    if (filter->frame_subtypes_set && (!metadata->frame_subtype_available ||
        metadata->frame_subtype > 15 || !(filter->frame_subtypes & (1U << metadata->frame_subtype)))) {
        atomic_fetch_add_explicit(&resources->counters.filtered_frame_subtype, 1, memory_order_relaxed);
        return false;
    }
    if (filter->minimum_rssi_set && metadata->rssi < filter->minimum_rssi) {
        atomic_fetch_add_explicit(&resources->counters.filtered_rssi, 1U,
                                  memory_order_relaxed);
        return false;
    }
    if (filter->valid_only) {
        bool invalid = false;

        if (metadata->first_word_invalid) {
            atomic_fetch_add_explicit(
                &resources->counters.filtered_first_word_invalid, 1U,
                memory_order_relaxed);
            invalid = true;
        }
        if (metadata->channel_estimate_valid_available &&
            !metadata->channel_estimate_valid) {
            atomic_fetch_add_explicit(
                &resources->counters.filtered_channel_estimate_invalid, 1U,
                memory_order_relaxed);
            invalid = true;
        }
        if (invalid) return false;
    }
    phase = resources->filter_phase;
    resources->filter_phase = phase >= filter->sample_every - 1U ? 0 : phase + 1U;
    if (phase != 0U) {
        atomic_fetch_add_explicit(
            &resources->counters.filtered_decimation, 1U,
            memory_order_relaxed);
        return false;
    }
    if (filter->maximum_rate_hz > 0U &&
        resources->last_accepted_timestamp_set) {
        uint32_t minimum_interval = (1000000U + filter->maximum_rate_hz - 1U) / filter->maximum_rate_hz;
        uint64_t elapsed = metadata->timestamp_us -
            resources->last_accepted_timestamp_us;

        if (metadata->timestamp_us < resources->last_accepted_timestamp_us || elapsed < minimum_interval) {
            atomic_fetch_add_explicit(
                &resources->counters.filtered_rate_limit, 1U,
                memory_order_relaxed);
            return false;
        }
    }
    resources->last_accepted_timestamp_us = metadata->timestamp_us;
    resources->last_accepted_timestamp_set = true;
    return true;
}

static void wifi_csi_slot_account_return(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    esp32_mquickjs_wifi_csi_slot_t *slot)
{
    bool expected = false;

    if (atomic_load_explicit(&slot->lease.returned, memory_order_acquire) &&
        atomic_compare_exchange_strong_explicit(
            &slot->return_accounted, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        atomic_fetch_sub_explicit(&resources->counters.leased_frames, 1U,
                                  memory_order_relaxed);
    }
}

static esp32_mquickjs_wifi_csi_publish_result_t wifi_csi_identity_exhausted(
    esp32_mquickjs_wifi_csi_resources_t *resources)
{
    atomic_store_explicit(&resources->identity_exhausted, true, memory_order_release);
    atomic_store_explicit(&resources->accepting, false, memory_order_release);
    atomic_fetch_add_explicit(&resources->counters.dropped_identity_exhausted, 1, memory_order_relaxed);
    return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_IDENTITY_EXHAUSTED;
}

esp32_mquickjs_wifi_csi_publish_result_t
esp32_mquickjs_wifi_csi_callback_publish(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_metadata_t *metadata,
    const uint8_t *payload,
    size_t length,
    const esp32_mquickjs_wifi_csi_packet_input_t *packet_input,
    esp32_mquickjs_wifi_csi_publish_fn publish,
    void *publish_opaque)
{
    esp32_mquickjs_wifi_csi_event_t event;
    esp32_mquickjs_wifi_csi_metadata_t normalized_metadata;
    esp32_mquickjs_wifi_csi_slot_t *slot;
    uint16_t slot_index;
    uint32_t slot_generation;

    if (resources == NULL || metadata == NULL || publish == NULL)
        return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_INVALID;
    if (payload == NULL || length == 0) {
        atomic_fetch_add_explicit(&resources->counters.invalid_callback_data, 1, memory_order_relaxed);
        return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_INVALID;
    }
    if (atomic_load_explicit(&resources->identity_exhausted, memory_order_acquire))
        return wifi_csi_identity_exhausted(resources);
    if (!atomic_load_explicit(&resources->accepting, memory_order_acquire)) {
        atomic_fetch_add_explicit(&resources->counters.dropped_closing, 1U,
                                  memory_order_relaxed);
        return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_CLOSING;
    }
    normalized_metadata = *metadata;
    normalized_metadata.address_mask = 0;
    memset(normalized_metadata.addresses, 0, sizeof(normalized_metadata.addresses));
    normalized_metadata.frame_type = ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN;
    normalized_metadata.frame_subtype_available = false;
    normalized_metadata.rx_sequence = UINT32_MAX;
    esp32_mquickjs_wifi_csi_packet_t packet = {0};
    esp32_mquickjs_wifi_csi_packet_result_t prepared = esp32_mquickjs_wifi_csi_packet_prepare(
        &resources->packet_options, packet_input, &packet);
    if (prepared == ESP32_MQUICKJS_WIFI_CSI_PACKET_OK) {
        normalized_metadata.address_mask = packet.header.address_mask;
        memcpy(normalized_metadata.addresses, packet.header.addresses, sizeof(normalized_metadata.addresses));
        normalized_metadata.frame_type = packet.header.type;
        normalized_metadata.frame_subtype = packet.header.subtype;
        normalized_metadata.frame_subtype_available = packet.header.frame_control_valid;
        if (packet.header.sequence_valid)
            normalized_metadata.rx_sequence = packet.header.sequence_control >> 4;
    }
    if (!esp32_mquickjs_wifi_csi_filter_accept(
            resources, &normalized_metadata)) {
        return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_FILTERED;
    }
    if (length > resources->max_frame_bytes) {
        atomic_fetch_add_explicit(
            &resources->counters.dropped_frame_too_large, 1U,
            memory_order_relaxed);
        return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_TOO_LARGE;
    }
    if (resources->packet_options.mode != ESP32_MQUICKJS_WIFI_CSI_PACKET_NONE) {
        if (prepared == ESP32_MQUICKJS_WIFI_CSI_PACKET_INCOMPLETE) {
            atomic_fetch_add_explicit(&resources->counters.dropped_packet_incomplete, 1, memory_order_relaxed);
            return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_PACKET_INCOMPLETE;
        }
        if (prepared != ESP32_MQUICKJS_WIFI_CSI_PACKET_OK) {
            atomic_fetch_add_explicit(&resources->counters.packet_unavailable, 1, memory_order_relaxed);
            if (prepared == ESP32_MQUICKJS_WIFI_CSI_PACKET_MALFORMED)
                atomic_fetch_add_explicit(&resources->counters.packet_malformed, 1, memory_order_relaxed);
            if (resources->packet_options.required || resources->packet_options.require_complete) {
                atomic_fetch_add_explicit(&resources->counters.dropped_packet_required, 1, memory_order_relaxed);
                return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_PACKET_REQUIRED;
            }
        }
    }
    if (packet.length > resources->max_packet_bytes) return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_INVALID;
    if (atomic_load_explicit(&resources->sequence, memory_order_relaxed) == UINT32_MAX)
        return wifi_csi_identity_exhausted(resources);
    if (!esp32_mquickjs_native_pool_acquire(&resources->pool, &slot_index)) {
        atomic_fetch_add_explicit(&resources->counters.dropped_pool_full, 1U,
                                  memory_order_relaxed);
        return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_POOL_FULL;
    }
    slot = &resources->slots[slot_index];
    if (slot->slot_generation == UINT32_MAX) {
        (void)esp32_mquickjs_native_pool_release(&resources->pool, slot_index);
        return wifi_csi_identity_exhausted(resources);
    }
    slot_generation = slot->slot_generation + 1U;
    slot->session_generation = resources->generation;
    slot->slot_generation = slot_generation;
    slot->sequence = atomic_fetch_add_explicit(
        &resources->sequence, 1U, memory_order_relaxed) + 1U;
    slot->metadata = normalized_metadata;
    slot->length = length;
    if (slot->packet != NULL) {
        uint8_t *destination = slot->packet->bytes;
        if (packet.length != 0) memcpy(destination, packet.bytes, packet.length);
        *slot->packet = packet;
        slot->packet->bytes = destination;
    }
    if (length > 0U) {
        memcpy(slot->payload, payload, length);
    }
    atomic_store_explicit(&slot->return_accounted, false,
                          memory_order_release);
    atomic_store_explicit(&slot->owner,
                          ESP32_MQUICKJS_WIFI_CSI_OWNER_EVENT,
                          memory_order_release);
    if (!esp32_mquickjs_native_lease_init(
            &slot->lease, &resources->pool, slot_index, slot_generation)) {
        (void)esp32_mquickjs_native_pool_release(
            &resources->pool, slot_index);
        atomic_store_explicit(&slot->return_accounted, true,
                              memory_order_release);
        return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_INVALID;
    }
    atomic_fetch_add_explicit(&resources->counters.leased_frames, 1U,
                              memory_order_relaxed);
    event.session_generation = resources->generation;
    event.slot_index = slot_index;
    event.slot_generation = slot_generation;
    if (!publish(&event, publish_opaque)) {
        (void)esp32_mquickjs_wifi_csi_slot_discard_event(resources, &event);
        atomic_fetch_add_explicit(&resources->counters.dropped_queue_full, 1U,
                                  memory_order_relaxed);
        return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_QUEUE_FULL;
    }
    if (packet.truncated)
        atomic_fetch_add_explicit(&resources->counters.packet_truncated, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&resources->counters.received_packet_bytes, packet.length, memory_order_relaxed);
    atomic_fetch_add_explicit(&resources->counters.accepted, 1U,
                              memory_order_relaxed);
    atomic_fetch_add_explicit(&resources->counters.received_bytes,
                              length > UINT32_MAX ? UINT32_MAX :
                                  (uint32_t)length,
                              memory_order_relaxed);
    return ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED;
}

esp32_mquickjs_wifi_csi_slot_t *esp32_mquickjs_wifi_csi_slot_from_event(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_event_t *event)
{
    esp32_mquickjs_wifi_csi_slot_t *slot;

    if (resources == NULL || event == NULL ||
        event->session_generation != resources->generation ||
        event->slot_index >= resources->capacity) {
        return NULL;
    }
    slot = &resources->slots[event->slot_index];
    if (slot->session_generation != event->session_generation ||
        slot->slot_generation != event->slot_generation ||
        esp32_mquickjs_native_lease_is_stale(
            &slot->lease, event->slot_generation)) {
        return NULL;
    }
    return slot;
}

bool esp32_mquickjs_wifi_csi_slot_retain(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    esp32_mquickjs_wifi_csi_slot_t *slot)
{
    return resources != NULL && slot != NULL &&
           slot->session_generation == resources->generation &&
           esp32_mquickjs_native_lease_retain(
               &slot->lease, slot->slot_generation);
}

bool esp32_mquickjs_wifi_csi_slot_release(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    esp32_mquickjs_wifi_csi_slot_t *slot)
{
    bool released;

    if (resources == NULL || slot == NULL ||
        slot->session_generation != resources->generation) {
        return false;
    }
    released = esp32_mquickjs_native_lease_release(
        &slot->lease, slot->slot_generation);
    wifi_csi_slot_account_return(resources, slot);
    return released;
}

bool esp32_mquickjs_wifi_csi_slot_take_event_owner(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_event_t *event)
{
    esp32_mquickjs_wifi_csi_slot_t *slot =
        esp32_mquickjs_wifi_csi_slot_from_event(resources, event);
    uint8_t expected = ESP32_MQUICKJS_WIFI_CSI_OWNER_EVENT;

    return slot != NULL && atomic_compare_exchange_strong_explicit(
        &slot->owner, &expected, ESP32_MQUICKJS_WIFI_CSI_OWNER_PUBLIC,
        memory_order_acq_rel, memory_order_acquire);
}

bool esp32_mquickjs_wifi_csi_slot_discard_event(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    const esp32_mquickjs_wifi_csi_event_t *event)
{
    esp32_mquickjs_wifi_csi_slot_t *slot =
        esp32_mquickjs_wifi_csi_slot_from_event(resources, event);
    uint8_t expected = ESP32_MQUICKJS_WIFI_CSI_OWNER_EVENT;
    bool closed;

    if (slot == NULL || !atomic_compare_exchange_strong_explicit(
            &slot->owner, &expected, ESP32_MQUICKJS_WIFI_CSI_OWNER_CLOSED,
            memory_order_acq_rel, memory_order_acquire)) {
        return false;
    }
    closed = esp32_mquickjs_native_lease_request_close(
        &slot->lease, slot->slot_generation);
    wifi_csi_slot_account_return(resources, slot);
    return closed;
}

bool esp32_mquickjs_wifi_csi_slot_close_public_owner(
    esp32_mquickjs_wifi_csi_resources_t *resources,
    esp32_mquickjs_wifi_csi_slot_t *slot)
{
    uint8_t expected = ESP32_MQUICKJS_WIFI_CSI_OWNER_PUBLIC;
    bool closed;

    if (resources == NULL || slot == NULL ||
        slot->session_generation != resources->generation) {
        return false;
    }
    if (!atomic_compare_exchange_strong_explicit(
            &slot->owner, &expected, ESP32_MQUICKJS_WIFI_CSI_OWNER_CLOSED,
            memory_order_acq_rel, memory_order_acquire)) {
        return false;
    }
    closed = esp32_mquickjs_native_lease_request_close(
        &slot->lease, slot->slot_generation);
    wifi_csi_slot_account_return(resources, slot);
    return closed;
}
