#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>
#include <stdint.h>

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_SOFTAP_SUPPORT && !CONFIG_ESP_HOST_WIFI_ENABLED && \
    (CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32S3)
#define ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE 1
#else
#define ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE 0
#endif

typedef struct {
    esp_err_t error, rollback_error;
    const char *stage;
    bool entered, mutated, verified, mode_ready, ap_quiesced, returned, rollback_attempted, rollback_complete;
} esp32_mquickjs_wifi_ap_prestart_result_t;

/* Caller owns the Radio lifecycle token and must retain it through release.
 * Capture is allocation-only; activate is the only native mutation entry. */
esp_err_t esp32_mquickjs_wifi_ap_prestart_prepare(uint32_t generation, uint32_t identity,
    const wifi_config_t *requested, const wifi_config_t *previous,
    bool (*accept)(const wifi_config_t *, const wifi_config_t *));
esp_err_t esp32_mquickjs_wifi_ap_prestart_activate(uint32_t generation, uint32_t identity,
    esp32_mquickjs_wifi_ap_prestart_result_t *result);
esp_err_t esp32_mquickjs_wifi_ap_prestart_release(uint32_t generation, uint32_t identity);
