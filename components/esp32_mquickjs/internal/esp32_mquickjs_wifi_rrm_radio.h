#pragma once
#include "esp32_mquickjs_wifi_rrm_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_RRM_SUPPORT
#include "esp32_mquickjs_wifi_radio.h"
/* Reserve before submission. The boot-scoped operation identity is the SDK
 * context, never a request address. An admitted token pins the Station lease
 * through callback return and a successful native retirement query. */
esp_err_t esp32_mquickjs_wifi_radio_rrm_reserve(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_operation_t *token);
esp_err_t esp32_mquickjs_wifi_rrm_reserve(esp32_mquickjs_wifi_radio_operation_t *token);
esp_err_t esp32_mquickjs_wifi_radio_rrm_command(
    const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_rrm_command_t command,
    esp32_mquickjs_wifi_rrm_callback_t callback,
    esp32_mquickjs_wifi_rrm_sdk_result_t *result);
/* Always queries on the supplicant task; only ownership==0 releases the exact
 * reservation. A local timeout or callback's terminal store is insufficient. */
esp_err_t esp32_mquickjs_wifi_radio_rrm_retire(
    esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_rrm_callback_t callback,
    esp32_mquickjs_wifi_rrm_sdk_result_t *result);
#endif
