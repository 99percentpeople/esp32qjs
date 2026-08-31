#include "wifi_csi_test_support.h"

#include <assert.h>

int main(void)
{
    wifi_csi_test_allocator_state_t state = {0};
    esp32_mquickjs_wifi_csi_allocator_t allocator =
        wifi_csi_test_allocator(&state);
    esp32_mquickjs_wifi_csi_resources_t resources;
    esp32_mquickjs_wifi_csi_metadata_t metadata = {0};
    esp32_mquickjs_wifi_csi_event_t event;
    uint8_t payload = 42;

    assert(esp32_mquickjs_wifi_csi_resources_init(
        &resources, 1, 1, 8, &allocator));
    esp32_mquickjs_wifi_csi_resources_set_accepting(&resources, true);
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, &payload, 1,
        wifi_csi_test_publish_ok, &event) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    esp32_mquickjs_wifi_csi_slot_t *slot =
        esp32_mquickjs_wifi_csi_slot_from_event(&resources, &event);
    assert(slot != NULL);
    assert(esp32_mquickjs_wifi_csi_slot_take_event_owner(
        &resources, &event));
    assert(esp32_mquickjs_wifi_csi_slot_retain(&resources, slot));
    assert(esp32_mquickjs_wifi_csi_slot_retain(&resources, slot));
    assert(esp32_mquickjs_native_lease_retain_count(&slot->lease) == 3);
    assert(esp32_mquickjs_wifi_csi_slot_close_public_owner(&resources, slot));
    assert(esp32_mquickjs_native_pool_available(&resources.pool) == 0);
    assert(atomic_load(&resources.counters.leased_frames) == 1);
    assert(esp32_mquickjs_wifi_csi_slot_release(&resources, slot));
    assert(atomic_load(&resources.counters.leased_frames) == 1);
    assert(esp32_mquickjs_wifi_csi_slot_release(&resources, slot));
    assert(atomic_load(&resources.counters.leased_frames) == 0);
    assert(esp32_mquickjs_native_pool_available(&resources.pool) == 1);
    assert(!esp32_mquickjs_wifi_csi_slot_release(&resources, slot));
    assert(atomic_load(&resources.counters.leased_frames) == 0);
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    return 0;
}
