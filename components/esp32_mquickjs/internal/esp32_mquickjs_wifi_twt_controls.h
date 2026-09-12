#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_wifi_he.h"
#include <stdint.h>
typedef enum {
    ESP32_MQUICKJS_WIFI_TWT_READ_CONFIG,
    ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,
    ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,
    ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,
} esp32_mquickjs_wifi_twt_control_kind_t;
typedef struct {
    wifi_twt_config_t config;
    uint32_t offset_us;
    uint8_t flow_bitmap;
} esp32_mquickjs_wifi_twt_control_t;
/* Radio owns initialization/lifecycle admission. Native dispatch checks the
 * current association and performs the entire command without yielding it.
 * The SDK's documented field and native-handler equivalents are retained. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_control(esp32_mquickjs_wifi_twt_control_kind_t kind,
    esp32_mquickjs_wifi_twt_control_t *value);
#endif
