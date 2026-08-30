#include "wifi_csi_test_support.h"

#include <assert.h>

static bool publish_reject(
    const esp32_mquickjs_wifi_csi_event_t *event, void *opaque)
{
    (void)event;
    (void)opaque;
    return false;
}

int main(void)
{
    wifi_csi_test_allocator_state_t state = {0};
    esp32_mquickjs_wifi_csi_allocator_t allocator =
        wifi_csi_test_allocator(&state);
    esp32_mquickjs_wifi_csi_resources_t resources;
    esp32_mquickjs_wifi_csi_metadata_t metadata = {0};
    esp32_mquickjs_wifi_csi_event_t first_event;
    uint8_t payload[9] = {0};

    assert(esp32_mquickjs_wifi_csi_resources_init(
        &resources, 1, 1, 8, &allocator));
    assert(!esp32_mquickjs_wifi_csi_callback_enter(&resources));
    esp32_mquickjs_wifi_csi_callback_leave(&resources);
    assert(atomic_load(&resources.callbacks_active) == 0);
    assert(atomic_load(&resources.counters.dropped_closing) == 1);

    esp32_mquickjs_wifi_csi_resources_set_accepting(&resources, true);
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, sizeof(payload),
        wifi_csi_test_publish_ok, &first_event) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_TOO_LARGE);
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, 8,
        publish_reject, NULL) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_QUEUE_FULL);
    assert(esp32_mquickjs_native_pool_available(&resources.pool) == 1);
    assert(atomic_load(&resources.counters.leased_frames) == 0);

    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, 8,
        wifi_csi_test_publish_ok, &first_event) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, 8,
        wifi_csi_test_publish_ok, &first_event) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_POOL_FULL);
    esp32_mquickjs_wifi_csi_slot_t *slot =
        esp32_mquickjs_wifi_csi_slot_from_event(&resources, &first_event);
    assert(slot != NULL);
    assert(esp32_mquickjs_wifi_csi_slot_request_close(&resources, slot));

    esp32_mquickjs_wifi_csi_resources_set_accepting(&resources, false);
    assert(esp32_mquickjs_wifi_csi_callback_publish(
        &resources, &metadata, payload, 8,
        wifi_csi_test_publish_ok, &first_event) ==
        ESP32_MQUICKJS_WIFI_CSI_PUBLISH_CLOSING);
    assert(atomic_load(&resources.counters.dropped_frame_too_large) == 1);
    assert(atomic_load(&resources.counters.dropped_queue_full) == 1);
    assert(atomic_load(&resources.counters.dropped_pool_full) == 1);
    assert(atomic_load(&resources.counters.dropped_closing) == 2);
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    return 0;
}
