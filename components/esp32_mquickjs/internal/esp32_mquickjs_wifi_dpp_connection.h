#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
#include "esp_wifi.h"
#include "esp_dpp.h"

typedef enum {
    ESP32_MQUICKJS_DPP_AUTH_DEFAULT,
    ESP32_MQUICKJS_DPP_AUTH_CONNECTOR,
    ESP32_MQUICKJS_DPP_AUTH_PSK,
    ESP32_MQUICKJS_DPP_AUTH_SAE,
} esp32_mquickjs_wifi_dpp_auth_t;

/* Pure preparation before native admission. DEFAULT selects Connector, then
 * SAE, then PSK according to the received AKM. A missing build capability never
 * lowers that choice. Explicit PSK/SAE selection on a hybrid row is permitted
 * only when that authentication was actually provisioned. */
esp_err_t esp32_mquickjs_wifi_dpp_connection_prepare(const esp_dpp_config_data_t *row,
    esp32_mquickjs_wifi_dpp_auth_t requested, esp32_mquickjs_wifi_dpp_auth_t *selected,
    wifi_config_t *station);
#endif
