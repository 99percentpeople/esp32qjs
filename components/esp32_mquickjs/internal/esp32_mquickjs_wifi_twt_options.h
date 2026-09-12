#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT
#include "esp_wifi_he.h"
#include <stdint.h>

#define ESP32_MQUICKJS_WIFI_TWT_DEFAULT_RESPONSE_MS 5000U
#define ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS 6000U
#define ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS 60000U
#define ESP32_MQUICKJS_WIFI_TWT_MAX_SLEEP_US (UINT64_C(1) << 35)

typedef struct {
    wifi_itwt_setup_config_t config;
    uint32_t timeout_ms;
    bool connection_id_set; /* Capture only: caller must reserve identity before SDK. */
} esp32_mquickjs_wifi_itwt_options_t;
typedef struct {
    wifi_btwt_setup_config_t config;
    uint32_t timeout_ms;
} esp32_mquickjs_wifi_btwt_options_t;

/* Native validation has no allocation/SDK/Radio side effects. Only fully valid
 * JS capture is committed to output; a failed capture leaves output zeroed.
 * These are internal capture contracts, not registered public entry points. */
bool esp32_mquickjs_wifi_itwt_options_valid(const esp32_mquickjs_wifi_itwt_options_t *options);
bool esp32_mquickjs_wifi_btwt_options_valid(const esp32_mquickjs_wifi_btwt_options_t *options);
uint64_t esp32_mquickjs_wifi_itwt_interval_us(const wifi_itwt_setup_config_t *config);
uint32_t esp32_mquickjs_wifi_itwt_duration_us(const wifi_itwt_setup_config_t *config);
bool esp32_mquickjs_wifi_capture_itwt(JSContext *ctx, JSValue input,
    esp32_mquickjs_wifi_itwt_options_t *output);
bool esp32_mquickjs_wifi_capture_btwt(JSContext *ctx, JSValue input,
    esp32_mquickjs_wifi_btwt_options_t *output);
#endif
