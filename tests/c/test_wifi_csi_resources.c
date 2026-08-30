#include "wifi_csi_test_support.h"

#include <assert.h>
#include <string.h>

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
    return 0;
}
