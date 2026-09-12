#pragma once
#include "esp_wps.h"
#include "esp32_mquickjs_wifi_wps_ap_result.h"

/* Native Wi-Fi task only. Caller retains every IPC input/output. Stop must
 * precede an ESP_TIMER_TASK drain and a later Wi-Fi task retire/checkpoint;
 * checkpoint alone is not a queue fence. Radio retains the AP throughout. */
esp_err_t esp32qjs_wps_ap_native_begin(const esp_wps_config_t *config, uint32_t *identity);
esp_err_t esp32qjs_wps_ap_native_start(uint32_t identity);
esp_err_t esp32qjs_wps_ap_native_stop(uint32_t identity);
esp_err_t esp32qjs_wps_ap_native_retire(uint32_t identity);
esp_err_t esp32qjs_wps_ap_native_checkpoint(uint32_t identity, uint32_t *revision);
esp_err_t esp32qjs_wps_ap_native_release(uint32_t identity, uint32_t revision);
/* SDK factory storage can exist even when device allocation failed. */
esp_err_t esp32qjs_wps_ap_factory_release(uint32_t identity);
