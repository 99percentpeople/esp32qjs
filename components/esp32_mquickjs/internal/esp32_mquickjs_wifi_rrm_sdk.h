#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_RRM_SUPPORT
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void (*esp32_mquickjs_wifi_rrm_callback_t)(void *identity, const uint8_t *report, size_t length);
typedef enum {
    ESP32_MQUICKJS_WIFI_RRM_SUBMIT,
    ESP32_MQUICKJS_WIFI_RRM_CANCEL,
    ESP32_MQUICKJS_WIFI_RRM_QUERY,
} esp32_mquickjs_wifi_rrm_command_t;
typedef struct {
    bool entered;
    bool tx_attempted; /* Includes SDK error after a possibly transmitted request. */
    int code; /* Exact SDK/errno result, separate from dispatch result. */
    int ownership; /* 0 no callback; 1 exact callback/context; negative foreign/inconsistent. */
} esp32_mquickjs_wifi_rrm_sdk_result_t;
/* Internal only. Caller must retain Radio lifecycle/operation protection and
 * native callback storage across SUBMIT until a subsequent QUERY/CANCEL proves
 * ownership==0. Context must be a nonzero boot-nonreused operation identity,
 * not a JS root/address. Callback cannot reenter these blocking functions. */
esp_err_t esp32_mquickjs_wifi_rrm_sdk_command(esp32_mquickjs_wifi_rrm_command_t command,
    esp32_mquickjs_wifi_rrm_callback_t callback, void *identity,
    esp32_mquickjs_wifi_rrm_sdk_result_t *result);
/* Call after recording the native control terminal, while callback bytes remain
 * valid. Observation allocation/event-queue failure cannot suppress that control. */
void esp32qjs_rrm_publish_observation(const uint8_t *report, size_t length);
#endif
