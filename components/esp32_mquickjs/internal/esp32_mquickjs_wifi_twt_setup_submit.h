#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include "esp_wifi_he_types.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    wifi_itwt_setup_config_t config; /* private SDK writeback, not AP result */
    uint32_t identity;
    esp_err_t sdk_error, driver_error, handoff_error;
    bool native_entered, driver_called, native_completed;
} esp32_mquickjs_wifi_twt_setup_dispatch_t;
/* Worker only, with Radio admission/lease held by the caller. Calls the real
 * public SDK API, preserving its preflight/capability/locking checks. No RF,
 * mode, power-save or connection policy is supplied by this helper.
 * output must have identity=0. A nonzero output identity remains owned even
 * on failure and must be retired. SDK error and handoff error are distinct.
 * No pointer to caller/runtime storage is passed to SDK. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_individual_submit(const wifi_itwt_setup_config_t *config,
    esp32_mquickjs_wifi_twt_setup_dispatch_t *output);
/* Native setup wrapper only. A foreign config is not a managed call; its
 * identity remains zero. An exact managed call is consumed at most once. */
esp_err_t esp32_mquickjs_wifi_twt_setup_submit_enter_native(const wifi_itwt_setup_config_t *config,
    uint32_t *identity);
bool esp32_mquickjs_wifi_twt_setup_submit_driver_native(const wifi_itwt_setup_config_t *config,
    uint32_t identity);
void esp32_mquickjs_wifi_twt_setup_submit_complete_native(const wifi_itwt_setup_config_t *config,
    uint32_t identity, esp_err_t driver_error);
#endif
