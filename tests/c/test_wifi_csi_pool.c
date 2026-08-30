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
    uint8_t payload[] = {1, 2, 3, 4};

    assert(esp32_mquickjs_wifi_csi_resources_init(
        &resources, 11, 2, 16, &allocator));
    esp32_mquickjs_wifi_csi_resources_set_accepting(&resources, true);
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, sizeof(payload),
        wifi_csi_test_publish_ok, &event) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    esp32_mquickjs_wifi_csi_slot_t *slot =
        esp32_mquickjs_wifi_csi_slot_from_event(&resources, &event);
    assert(slot != NULL);
    assert(slot->sequence == 1);
    assert(slot->length == sizeof(payload));
    assert(slot->payload[3] == 4);

    esp32_mquickjs_wifi_csi_event_t stale = event;
    stale.slot_generation++;
    assert(esp32_mquickjs_wifi_csi_slot_from_event(&resources, &stale) == NULL);
    stale = event;
    stale.session_generation++;
    assert(esp32_mquickjs_wifi_csi_slot_from_event(&resources, &stale) == NULL);

    assert(esp32_mquickjs_wifi_csi_slot_request_close(&resources, slot));
    assert(!esp32_mquickjs_wifi_csi_slot_request_close(&resources, slot));
    assert(atomic_load(&resources.counters.leased_frames) == 0);
    assert(esp32_mquickjs_native_pool_available(&resources.pool) == 2);
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    return 0;
}
