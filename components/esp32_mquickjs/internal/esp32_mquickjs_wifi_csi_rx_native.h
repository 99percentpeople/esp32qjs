#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
#include "esp_wifi.h"
#include <stddef.h>

/* The output is borrowed only for this synchronous CSI callback. Its extent
 * comes from completed native copies, not sig_len or allocator rounding. */
typedef struct {
    const uint8_t *bytes;
    size_t readable_bytes;
} esp32_mquickjs_wifi_csi_rx_packet_t;

size_t esp32_mquickjs_wifi_csi_rx_native_control_bytes(void);
void esp32_mquickjs_wifi_csi_rx_native_enable(bool enabled);
bool esp32_mquickjs_wifi_csi_rx_native_take(const wifi_csi_info_t *info,
    esp32_mquickjs_wifi_csi_rx_packet_t *packet);
#endif
