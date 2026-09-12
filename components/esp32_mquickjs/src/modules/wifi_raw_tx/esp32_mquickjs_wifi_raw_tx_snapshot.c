#include "esp32_mquickjs_wifi_raw_tx_snapshot.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include <string.h>

bool esp32_mquickjs_wifi_raw_tx_snapshot(const esp_80211_tx_info_t *info,
    uint64_t callback_time_us, esp32_mquickjs_wifi_raw_tx_snapshot_t *output)
{
    if (info == NULL || output == NULL || (info->ifidx != WIFI_IF_STA && info->ifidx != WIFI_IF_AP)) return false;
    _Static_assert(sizeof(info->data_len) == 1, "Review Raw TX body-length semantics when the SDK type changes");
    esp32_mquickjs_wifi_raw_tx_snapshot_t result = {
        .callback_time_us = callback_time_us,
        .interface = info->ifidx == WIFI_IF_STA ? ESP32_MQUICKJS_WIFI_RAW_TX_STATION : ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT,
        .status = info->tx_status == WIFI_SEND_SUCCESS ? ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS :
            info->tx_status == WIFI_SEND_FAIL ? ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED : ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_UNKNOWN,
        .raw_status = (int32_t)info->tx_status,
        .raw_rate = (int32_t)info->rate,
        .raw_body_length = info->data_len,
    };
    if (info->src_addr != NULL) { memcpy(result.source, info->src_addr, sizeof(result.source)); result.source_available = true; }
    if (info->des_addr != NULL) { memcpy(result.destination, info->des_addr, sizeof(result.destination)); result.destination_available = true; }
    memcpy(output, &result, sizeof(result));
    return true;
}
#endif
