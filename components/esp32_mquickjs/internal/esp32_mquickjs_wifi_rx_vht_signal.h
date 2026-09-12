#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Pure decode of the fixed SDK esp_wifi_vht_siga1_t word. No SDK pointer,
 * target capability, channel ownership or CSI layout is inferred here. */
typedef struct {
    uint8_t bandwidth_mhz, mcs;
    bool mcs_available, stbc, short_gi, fec_available, ldpc;
} esp32_mquickjs_wifi_rx_vht_signal_t;

static inline esp32_mquickjs_wifi_rx_vht_signal_t esp32_mquickjs_wifi_rx_decode_vht_signal(
    uint32_t signal, bool reported_multi_user)
{
    uint8_t group = (uint8_t)((signal >> 4U) & 0x3fU);
    bool single_user = !reported_multi_user && (group == 0U || group == 63U);
    uint8_t mcs = (uint8_t)((signal >> 28U) & 0xfU);
    /* CBW is nominal PPDU width. 160 does not distinguish contiguous 160 from
     * 80+80, nor identify center frequencies or the receiver's captured width. */
    esp32_mquickjs_wifi_rx_vht_signal_t result = {
        .bandwidth_mhz = (uint8_t)(20U << (signal & 3U)),
        .mcs = single_user && mcs <= 9U ? mcs : 0U,
        .mcs_available = single_user && mcs <= 9U,
        .stbc = (signal & (1UL << 3U)) != 0U,
        .short_gi = (signal & (1UL << 24U)) != 0U,
        .fec_available = single_user,
        .ldpc = single_user && (signal & (1UL << 26U)) != 0U,
    };
    return result;
}
