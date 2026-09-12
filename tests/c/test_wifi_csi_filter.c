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
        .addresses = {{1, 2, 3, 4, 5, 6}, {6, 5, 4, 3, 2, 1}},
        .address_mask = 3,
        .rssi = -45,
        .timestamp_us = 1000,
        .channel_estimate_valid = true,
        .channel_estimate_valid_available = true,
    };

    assert(esp32_mquickjs_wifi_csi_resources_init(
        &resources, 1, 4, 64,0, &allocator));
    memcpy(resources.filter.source_macs[0], metadata.addresses[0], 6);
    memcpy(resources.filter.destination_macs[0], metadata.addresses[1], 6);
    resources.filter.source_mac_count = 1;
    resources.filter.destination_mac_count = 1;
    resources.filter.minimum_rssi = -50;
    resources.filter.minimum_rssi_set = true;
    assert(esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));

    metadata.addresses[0][0] = 9;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.addresses[0][0] = 1;
    metadata.rssi = -51;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.rssi = -45;
    metadata.channel_estimate_valid = false;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.channel_estimate_valid = true;
    metadata.first_word_invalid = true;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.first_word_invalid = false;

    resources.filter.sample_every = 2;
    resources.filter_phase = 0;
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
    assert(atomic_load(
        &resources.counters.filtered_channel_estimate_invalid) == 1);
    assert(atomic_load(
        &resources.counters.filtered_first_word_invalid) == 1);
    assert(atomic_load(&resources.counters.filtered_decimation) == 1);
    assert(atomic_load(&resources.counters.filtered_rate_limit) == 1);
    resources.filter.maximum_rate_hz = 3;
    resources.last_accepted_timestamp_set = false;
    metadata.timestamp_us = 1000000;
    assert(esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.timestamp_us = 1333333;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.timestamp_us = 1333334;
    assert(esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    metadata.timestamp_us = 1;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    resources.filter.maximum_rate_hz = 0;
    resources.filter.sample_every = UINT32_MAX;
    resources.filter_phase = UINT32_MAX - 1;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    assert(resources.filter_phase == 0);
    assert(esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    resources.filter.source_mac_count = ESP32_MQUICKJS_WIFI_CSI_MAX_MAC_FILTERS + 1;
    assert(!esp32_mquickjs_wifi_csi_filter_accept(&resources, &metadata));
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    return 0;
}
