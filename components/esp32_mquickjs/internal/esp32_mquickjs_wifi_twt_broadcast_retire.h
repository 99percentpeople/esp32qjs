#pragma once
#include "esp32_mquickjs_wifi_twt_fence.h"
#include "esp32_mquickjs_wifi_twt_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
/* Stable owner storage until poll returns OK. Serialized worker use only,
 * never called from native/timer/event task. Public timeout/GC cannot free it.
 * Does not submit teardown: the owner submits it once, then polls this same
 * coordinator. The saved teardown snapshot survives native singleton release
 * and any remaining marker-clear retry. */
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    esp32_mquickjs_wifi_twt_fence_t fence;
    esp32_mquickjs_wifi_btwt_cut_t cut;
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t teardown;
    uint32_t identity, sequence, slot;
    esp_err_t error;
    const char *stage;
    bool released;
} esp32_mquickjs_wifi_btwt_retire_t;
esp_err_t esp32_mquickjs_wifi_btwt_retire_poll(esp32_mquickjs_wifi_btwt_retire_t *,
    const esp32_mquickjs_wifi_twt_token_t *, unsigned slot, uint32_t identity);
#endif
