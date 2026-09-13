#pragma once

#include "esp32_mquickjs_types.h"

/* PV0 MAC type/subtype identity. A null name preserves an unknown/reserved
 * numeric identity; a descriptor is not proof of parsing or TX support. */
JSValue esp32_mquickjs_wifi_frame_type_to_js(JSContext *ctx, unsigned type, unsigned subtype);

/* Named layouts understood by the common RX parser, not a TX capability. */
JSValue esp32_mquickjs_wifi_frame_types_to_js(JSContext *ctx);
