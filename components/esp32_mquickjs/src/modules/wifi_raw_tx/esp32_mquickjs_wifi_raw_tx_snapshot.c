#include "esp32_mquickjs_wifi_raw_tx_snapshot.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include <string.h>

const char *esp32_mquickjs_wifi_raw_tx_mac_status_name(uint8_t mac_status_byte)
{
    /* TX-info writers in the pinned SDK: success/recycle, frame-exchange
     * discard (2/3), and MPDU/aged-MSDU discard (4). These are framework
     * observation labels, not public SDK enum symbols or RF/ACK evidence.
     * LMAC context[19] is a DIFFERENT field: its 7/8/9 values do not establish
     * meanings for the descriptor's TX-info[19]. Leave those names unknown. */
    switch (mac_status_byte) {
    case 1: return "success";
    case 2:
    case 3: return "frame-exchange";
    case 4: return "discarded";
    default: return NULL;
    }
}

bool esp32_mquickjs_wifi_raw_tx_snapshot(const esp_80211_tx_info_t *info,
    uint64_t callback_time_us, int mac_status_byte,
    esp32_mquickjs_wifi_raw_tx_snapshot_t *output)
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
        .mac_status_available = mac_status_byte >= 0 && mac_status_byte <= 255,
        .mac_status_byte = mac_status_byte >= 0 && mac_status_byte <= 255 ? (uint8_t)mac_status_byte : 0,
    };
    if (info->src_addr != NULL) { memcpy(result.source, info->src_addr, sizeof(result.source)); result.source_available = true; }
    if (info->des_addr != NULL) { memcpy(result.destination, info->des_addr, sizeof(result.destination)); result.destination_available = true; }
    memcpy(output, &result, sizeof(result));
    return true;
}
#endif
