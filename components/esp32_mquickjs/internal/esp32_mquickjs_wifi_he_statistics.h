#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE \
    (CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_IDF_TARGET_ESP32C5 && !CONFIG_ESP_HOST_WIFI_ENABLED)

typedef struct {
    bool ordinary, multi_user;
    uint8_t tx_mask; /* SDK ACI: voice=0, video=1, best effort=2, background=3. */
} esp32_mquickjs_wifi_he_statistics_t;

#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
/* Synchronous Wi-Fi-task observation of native storage; no counters or native
 * pointers escape. Retire captures the enabled configuration and frees all
 * statistics before STOP. Caller owns the Radio mutation mutex and driver. */
esp_err_t esp32_mquickjs_wifi_he_statistics_snapshot(esp32_mquickjs_wifi_he_statistics_t *actual, bool retire);
/* Started Radio only. Successful prefix is retained; never reset RX counters
 * again merely because a later ACI allocation failed. */
esp_err_t esp32_mquickjs_wifi_he_statistics_restore(const esp32_mquickjs_wifi_he_statistics_t *wanted,
    uint8_t *completed);
#endif

static inline bool wifi_he_statistics_equal(const esp32_mquickjs_wifi_he_statistics_t *a,
    const esp32_mquickjs_wifi_he_statistics_t *b)
{
    return a->ordinary == b->ordinary && a->multi_user == b->multi_user && a->tx_mask == b->tx_mask;
}
