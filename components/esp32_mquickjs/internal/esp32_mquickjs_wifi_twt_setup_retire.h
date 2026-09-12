#pragma once
#include "esp32_mquickjs_wifi_twt_fence.h"
#include "esp32_mquickjs_wifi_twt_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
/* Stable zero-initialized owner storage, serialized outside native/event/timer
 * tasks. Keep alive through timeout/GC until poll returns ESP_OK. This retires
 * a cancelled pending setup or an acknowledged, locally quiescent single-flow
 * teardown. Submission and Radio lease ownership belong to the caller; this
 * does not prove RF late-frame exclusion or information TX identity. */
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    esp32_mquickjs_wifi_twt_fence_t fence;
    esp32_mquickjs_wifi_twt_setup_cut_t cut;
    uint32_t identity, event_sequence;
    esp_err_t error;
    const char *stage;
    bool result_released, released;
} esp32_mquickjs_wifi_twt_setup_retire_t;
esp_err_t esp32_mquickjs_wifi_twt_setup_retire_poll(esp32_mquickjs_wifi_twt_setup_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t identity);
#endif
