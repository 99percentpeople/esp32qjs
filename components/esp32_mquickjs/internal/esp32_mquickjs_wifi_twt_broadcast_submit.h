#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include "esp_wifi_he_types.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    wifi_btwt_setup_config_t config;
    uint32_t identity; /* Exact TX request, zero if output was not reached. */
    esp_err_t sdk_error, driver_error, handoff_error;
    bool native_entered, driver_called, native_completed;
} esp32_mquickjs_wifi_btwt_dispatch_t;
/* Worker only, caller holds Radio admission/lease. The public SDK still owns
 * preflight and synchronous ioctl. Passes boot-owned config, never JS/Future
 * storage. Nonzero result identity remains held even on error or close; its
 * eventual owner must establish full native retirement before releasing it.
 * Uncertain SDK return retains the dispatch buffer and rejects reuse. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_submit(const wifi_btwt_setup_config_t *,
    esp32_mquickjs_wifi_btwt_dispatch_t *);
/* Native process hooks. Foreign config does not acquire an invented owner. */
esp_err_t esp32_mquickjs_wifi_btwt_submit_enter_native(const wifi_btwt_setup_config_t *, bool *managed);
bool esp32_mquickjs_wifi_btwt_submit_driver_native(const wifi_btwt_setup_config_t *);
void esp32_mquickjs_wifi_btwt_submit_complete_native(const wifi_btwt_setup_config_t *, esp_err_t);
/* Called after result reservation and BEFORE SDK output, on the exact native
 * submission task. Pins the result and hands its number to the dispatch. */
esp_err_t esp32_mquickjs_wifi_btwt_submit_bind_native(unsigned slot, uint32_t identity,
    const uint8_t parameter[17]);
#endif
