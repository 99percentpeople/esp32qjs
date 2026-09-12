#pragma once
#include "sdkconfig.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Fixed Neighbor Report IE body; unknown subelements are counted, not copied. */
typedef struct {
    uint32_t bssid_information;
    uint8_t bssid[6], operating_class, channel, phy_type, preference;
    bool has_preference;
    uint16_t skipped_subelements;
} esp32_mquickjs_wifi_neighbor_t;
/* Output unchanged on invalid/truncated/duplicate preference subelements. */
bool esp32_mquickjs_wifi_neighbor_decode(esp32_mquickjs_wifi_neighbor_t *output,
    const uint8_t *body, size_t length);
#endif
