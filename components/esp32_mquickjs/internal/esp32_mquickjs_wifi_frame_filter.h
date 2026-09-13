#pragma once
#include "esp32_mquickjs_types.h"
#include <stdbool.h>
#include <stdint.h>

/* Capture at most 64 distinct PV0 pairs; output changes only on success.
 * An empty list produces four zero masks. Missing filters are handled by callers. */
bool esp32_mquickjs_wifi_frame_filter_parse(JSContext *ctx, JSValue value, uint16_t output[4]);
JSValue esp32_mquickjs_wifi_frame_filter_to_js(JSContext *ctx, const uint16_t masks[4]);
