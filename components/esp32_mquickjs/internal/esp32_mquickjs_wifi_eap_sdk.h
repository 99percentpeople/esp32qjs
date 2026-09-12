#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
/* Exact build-local SDK bridge flags, not a complete driver ownership query.
 * ENABLED is the software EAP API state, not the driver byte.
 * DRIVER bits track reviewed writes, not a live driver getter. Zero reports
 * cleared tracked resources; Radio/caller ownership must still be established. */
enum {
    ESP32_MQUICKJS_WIFI_EAP_SDK_ENABLED = 1U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_SM = 2U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_TASK = 4U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_QUEUE = 8U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_DATA_LOCK = 16U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_SYNC_SEM = 32U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_EXIT_SEM = 64U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_RETIRING = 128U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_EXIT_PENDING = 256U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_DRIVER_UNKNOWN = 512U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_DRIVER_ENABLED = 1024U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_CALLBACKS = 2048U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_METHODS = 4096U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_GLOBAL_CREDENTIALS = 8192U,
    ESP32_MQUICKJS_WIFI_EAP_SDK_CALLBACK_QUARANTINE = 16384U,
};
typedef struct {
    bool entered;
    bool time_check_known;
    bool disable_time_check; /* Actual SDK getter, not the configured profile. */
    uint32_t resources; /* UINT32_MAX until actual ordered SDK observation. */
    esp_err_t cleanup_error;
    esp_err_t control_error; /* Last control that actually executed on the SDK task. */
} esp32_mquickjs_wifi_eap_sdk_snapshot_t;
/* Synchronous native dispatch; no JS pointers and no mutation. Caller must not
 * hold an EAP/SDK lock that the dispatched Wi-Fi task needs. */
esp_err_t esp32_mquickjs_wifi_eap_sdk_snapshot(esp32_mquickjs_wifi_eap_sdk_snapshot_t *output);
#endif
