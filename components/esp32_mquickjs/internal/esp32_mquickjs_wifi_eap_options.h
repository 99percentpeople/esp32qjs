#pragma once
#include "esp32_mquickjs_wifi_enterprise_profile.h"
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
/* Synchronous capture only: no Radio/SDK mutation and no JS pointer escapes.
 * On failure output is NULL and partial native secrets are wiped. */
bool esp32_mquickjs_wifi_eap_capture(JSContext *ctx, JSValue options,
    esp32_mquickjs_wifi_eap_profile_t **output);
#endif
