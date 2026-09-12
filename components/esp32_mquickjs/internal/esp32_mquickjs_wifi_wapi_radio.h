#pragma once
#include "esp32_mquickjs_wifi_wapi.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WAPI_PSK
#include "esp32_mquickjs_wifi_radio.h"
esp_err_t esp32_mquickjs_wifi_radio_wapi_prepare(bool enabled, bool *rebuild);
esp_err_t esp32_mquickjs_wifi_radio_wapi_select(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode, bool enabled);
#endif
