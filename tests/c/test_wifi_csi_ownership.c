#include "wifi_csi_test_support.h"

#include <assert.h>
#include <string.h>

typedef struct {
    esp32_mquickjs_wifi_csi_event_t events[2];
    size_t count;
} fake_queue_t;

static bool fake_queue_publish(
    const esp32_mquickjs_wifi_csi_event_t *event, void *opaque)
{
    fake_queue_t *queue = opaque;

    if (queue->count >= 2U) return false;
    queue->events[queue->count++] = *event;
    return true;
}

int main(void)
{
    wifi_csi_test_allocator_state_t state = {0};
    esp32_mquickjs_wifi_csi_allocator_t allocator =
        wifi_csi_test_allocator(&state);
    esp32_mquickjs_wifi_csi_resources_t resources;
    esp32_mquickjs_wifi_csi_metadata_t metadata = {0};
    fake_queue_t queue = {0};
    uint8_t payload[] = {1U, 2U, 3U, 4U};
    uint8_t owned_copy[sizeof(payload)];
    esp32_mquickjs_wifi_csi_slot_t *slot;

    assert(esp32_mquickjs_wifi_csi_resources_init(
        &resources, 9U, 3U, sizeof(payload), &allocator));
    esp32_mquickjs_wifi_csi_resources_set_accepting(&resources, true);

    metadata.driver_timestamp_us = 0xfffffff0U;
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, sizeof(payload),
        fake_queue_publish, &queue) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    metadata.driver_timestamp_us = 0x20U;
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, sizeof(payload),
        fake_queue_publish, &queue) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, sizeof(payload),
        fake_queue_publish, &queue) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_QUEUE_FULL);
    assert(queue.count == 2U);
    assert(atomic_load(&resources.counters.leased_frames) == 2U);
    assert(esp32_mquickjs_native_pool_available(&resources.pool) == 1U);

    slot = esp32_mquickjs_wifi_csi_slot_from_event(
        &resources, &queue.events[0]);
    assert(slot != NULL);
    assert(slot->metadata.timestamp_us == 0xfffffff0ULL);
    assert(esp32_mquickjs_wifi_csi_slot_take_event_owner(
        &resources, &queue.events[0]));
    assert(!esp32_mquickjs_wifi_csi_slot_take_event_owner(
        &resources, &queue.events[0]));

    memcpy(owned_copy, slot->payload, slot->length);
    assert(memcmp(owned_copy, payload, sizeof(payload)) == 0);
    assert(esp32_mquickjs_native_lease_retain_count(&slot->lease) == 1U);

    assert(esp32_mquickjs_wifi_csi_slot_retain(&resources, slot));
    assert(esp32_mquickjs_wifi_csi_slot_retain(&resources, slot));
    assert(esp32_mquickjs_wifi_csi_slot_close_public_owner(
        &resources, slot));
    assert(!esp32_mquickjs_wifi_csi_slot_close_public_owner(
        &resources, slot));
    assert(atomic_load(&resources.counters.leased_frames) == 2U);
    assert(esp32_mquickjs_wifi_csi_slot_release(&resources, slot));
    assert(atomic_load(&resources.counters.leased_frames) == 2U);
    assert(esp32_mquickjs_wifi_csi_slot_release(&resources, slot));
    assert(atomic_load(&resources.counters.leased_frames) == 1U);

    slot = esp32_mquickjs_wifi_csi_slot_from_event(
        &resources, &queue.events[1]);
    assert(slot != NULL);
    assert(slot->metadata.timestamp_us == 0x100000020ULL);

    assert(esp32_mquickjs_wifi_csi_slot_discard_event(
        &resources, &queue.events[1]));
    assert(!esp32_mquickjs_wifi_csi_slot_discard_event(
        &resources, &queue.events[1]));
    assert(atomic_load(&resources.counters.leased_frames) == 0U);
    assert(esp32_mquickjs_native_pool_available(&resources.pool) == 3U);
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    assert(state.allocations == 0U);
    return 0;
}
