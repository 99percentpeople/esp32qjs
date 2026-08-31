#include "esp32_mquickjs_wifi_csi_batch.h"

#include <limits.h>

bool esp32_mquickjs_wifi_csi_batch_options_valid(
    uint32_t maximum_frames,
    uint32_t minimum_frames,
    uint32_t timeout_ms,
    uint32_t maximum_latency_ms,
    uint32_t build_maximum_frames)
{
    return build_maximum_frames > 0U && maximum_frames > 0U &&
           minimum_frames > 0U && minimum_frames <= maximum_frames &&
           maximum_frames <= build_maximum_frames &&
           timeout_ms <= INT32_MAX && maximum_latency_ms <= INT32_MAX;
}

bool esp32_mquickjs_wifi_csi_batch_should_return(
    uint32_t frame_count,
    uint32_t minimum_frames,
    uint32_t maximum_frames,
    uint32_t maximum_latency_ms,
    bool window_expired)
{
    return frame_count > 0U &&
           (frame_count >= maximum_frames || frame_count >= minimum_frames ||
            maximum_latency_ms == 0U || window_expired);
}
