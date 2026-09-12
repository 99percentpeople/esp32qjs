#pragma once
#include "esp32_mquickjs_wifi_twt_fence.h"
#include "esp32_mquickjs_wifi_twt_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
/* Zero-initialized stable native owner storage. The owner serializes calls
 * outside the native Wi-Fi/event/timer tasks and keeps this address alive
 * until poll returns ESP_OK. Public deadline/GC must not discard it. */
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    esp32_mquickjs_wifi_twt_fence_t fence;
    esp32_mquickjs_wifi_twt_probe_cut_t cut;
    uint32_t identity, event_sequence;
    esp_err_t error;
    const char *stage;
    bool cancelled, released;
} esp32_mquickjs_wifi_twt_probe_retire_t;
/* Exact managed probe only. Cancels native authority, waits for TX pins,
 * orders TASK timer/native queue, posts the copied-number event fence, and
 * releases the native pin only after the same cut is rechecked by ioctl.
 * NOT_FINISHED means pending; other errors retain the unfinished suffix.
 * ESP_OK retires this probe and its marker storage, not any TWT Agreement or
 * the caller's Radio lease. The owner must release that lease separately. */
esp_err_t esp32_mquickjs_wifi_twt_probe_retire_poll(esp32_mquickjs_wifi_twt_probe_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t identity);
#endif
