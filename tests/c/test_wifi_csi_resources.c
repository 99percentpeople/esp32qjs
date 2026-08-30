#include "wifi_csi_test_support.h"

#include <assert.h>
#include <string.h>

static void test_repeated_resource_lifecycle_returns_to_baseline(void)
{
    wifi_csi_test_allocator_state_t state = {0};
    esp32_mquickjs_wifi_csi_allocator_t allocator =
        wifi_csi_test_allocator(&state);
    esp32_mquickjs_wifi_csi_metadata_t metadata = {0};
    uint8_t payload[32] = {0};

    for (uint32_t generation = 1; generation <= 500U; ++generation) {
        esp32_mquickjs_wifi_csi_resources_t resources;
        esp32_mquickjs_wifi_csi_event_t event;
        esp32_mquickjs_wifi_csi_slot_t *slot;

        assert(esp32_mquickjs_wifi_csi_resources_init(
            &resources, generation, 4, sizeof(payload), &allocator));
        assert(state.allocations == 2);
        esp32_mquickjs_wifi_csi_resources_set_accepting(&resources, true);
        assert(esp32_mquickjs_wifi_csi_callback_publish(
            &resources, &metadata, payload, sizeof(payload),
            wifi_csi_test_publish_ok, &event) ==
            ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
        slot = esp32_mquickjs_wifi_csi_slot_from_event(&resources, &event);
        assert(slot != NULL);
        assert(!esp32_mquickjs_wifi_csi_resources_deinit(&resources));
        assert(esp32_mquickjs_wifi_csi_slot_request_close(&resources, slot));
        assert(atomic_load(&resources.counters.leased_frames) == 0U);
        assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
        assert(state.allocations == 0);
    }
}

int main(void)
{
    esp32_mquickjs_wifi_csi_resources_t resources;

    for (unsigned fail_at = 1; fail_at <= 2; ++fail_at) {
        wifi_csi_test_allocator_state_t state = {.fail_at = fail_at};
        esp32_mquickjs_wifi_csi_allocator_t allocator =
            wifi_csi_test_allocator(&state);

        memset(&resources, 0xa5, sizeof(resources));
        assert(!esp32_mquickjs_wifi_csi_resources_init(
            &resources, 1, 4, 128, &allocator));
        assert(state.allocations == 0);
        assert(resources.slots == NULL);
        assert(resources.payload_storage == NULL);
    }

    wifi_csi_test_allocator_state_t state = {0};
    esp32_mquickjs_wifi_csi_allocator_t allocator =
        wifi_csi_test_allocator(&state);
    assert(esp32_mquickjs_wifi_csi_resources_init(
        &resources, 7, 4, 128, &allocator));
    assert(state.allocations == 2);
    assert(resources.slots[1].payload - resources.slots[0].payload == 128);
    atomic_store(&resources.callbacks_active, 1);
    assert(!esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    atomic_store(&resources.callbacks_active, 0);
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    assert(state.allocations == 0);
    assert(!esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    test_repeated_resource_lifecycle_returns_to_baseline();
    return 0;
}
