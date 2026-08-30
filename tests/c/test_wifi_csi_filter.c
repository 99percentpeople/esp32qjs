#include "wifi_csi_test_support.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    wifi_csi_test_allocator_state_t state = {0};
    esp32_mquickjs_wifi_csi_allocator_t allocator =
        wifi_csi_test_allocator(&state);
    esp32_mquickjs_wifi_csi_resources_t resources;
    esp32_mquickjs_wifi_csi_metadata_t metadata = {
        .source_mac = {1, 2, 3, 4, 5, 6},
        .destination_mac = {6, 5, 4, 3, 2, 1},
        .rssi = -45,
        .timestamp_us = 1000,
        .destination_mac_available = true,
        .channel_estimate_valid = true,
        .channel_estimate_valid_available = true,
    };

    assert(esp32_mquickjs_wifi_csi_resources_init(
        &resources, 1, 4, 64, &allocator));
    memcpy(resources.filter.source_macs[0], metadata.source_mac, 6);
    memcpy(resources.filter.destination_macs[0], metadata.destination_mac, 6);
    resources.filter.source_mac_count = 1;
    resources.filter.destination_mac_count = 1;
    resources.filter.minimum_rssi = -50;
    resources.filter.minimum_rssi_set = true;
    assert(esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));

    metadata.source_mac[0] = 9;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.source_mac[0] = 1;
    metadata.rssi = -51;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.rssi = -45;
    metadata.channel_estimate_valid = false;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.channel_estimate_valid = true;

    resources.filter.sample_every = 2;
    resources.filter_qualified = 0;
    resources.last_accepted_timestamp_set = false;
    assert(esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));

    resources.filter.sample_every = 1;
    resources.filter.maximum_rate_hz = 1000;
    resources.last_accepted_timestamp_set = false;
    metadata.timestamp_us = 5000;
    assert(esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.timestamp_us = 5500;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.timestamp_us = 6000;
    assert(esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));

    assert(atomic_load(&resources.counters.filtered_mac) == 1);
    assert(atomic_load(&resources.counters.filtered_rssi) == 1);
    assert(atomic_load(&resources.counters.invalid_channel_estimate) == 1);
    assert(atomic_load(&resources.counters.filtered_decimation) == 1);
    assert(atomic_load(&resources.counters.filtered_rate_limit) == 1);
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    return 0;
}
