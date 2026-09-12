#include "esp32_mquickjs_wifi_neighbor.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include <string.h>
bool esp32_mquickjs_wifi_neighbor_decode(esp32_mquickjs_wifi_neighbor_t *output,
    const uint8_t *body, size_t length)
{
    if (output == NULL || body == NULL || length < 13U || length > 255U) return false;
    esp32_mquickjs_wifi_neighbor_t value = {0};
    memcpy(value.bssid, body, 6);
    value.bssid_information = (uint32_t)body[6] | (uint32_t)body[7] << 8 |
        (uint32_t)body[8] << 16 | (uint32_t)body[9] << 24;
    value.operating_class = body[10];
    value.channel = body[11];
    value.phy_type = body[12];
    for (size_t offset = 13; offset < length;) {
        if (length - offset < 2U) return false;
        uint8_t id = body[offset], size = body[offset + 1U];
        offset += 2U;
        if (size > length - offset) return false;
        if (id == 3U) {
            if (size != 1U || value.has_preference) return false;
            value.has_preference = true;
            value.preference = body[offset];
        } else ++value.skipped_subelements;
        offset += size;
    }
    *output = value;
    return true;
}
#endif
