#ifndef ESP32_MQUICKJS_WIFI_CSI_BATCH_H
#define ESP32_MQUICKJS_WIFI_CSI_BATCH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool esp32_mquickjs_wifi_csi_batch_options_valid(
    uint32_t maximum_frames,
    uint32_t minimum_frames,
    uint32_t timeout_ms,
    uint32_t maximum_latency_ms,
    uint32_t build_maximum_frames);

bool esp32_mquickjs_wifi_csi_batch_should_return(
    uint32_t frame_count,
    uint32_t minimum_frames,
    uint32_t maximum_frames,
    uint32_t maximum_latency_ms,
    bool window_expired);

#ifdef __cplusplus
}
#endif

#endif
