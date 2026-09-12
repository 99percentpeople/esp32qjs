#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Internal signal-layout selector, independent of SDK/wire enum values. */
typedef enum {
    ESP32_MQUICKJS_WIFI_RX_HE_SU,
    ESP32_MQUICKJS_WIFI_RX_HE_MU,
    ESP32_MQUICKJS_WIFI_RX_HE_ER_SU,
    ESP32_MQUICKJS_WIFI_RX_HE_TB,
} esp32_mquickjs_wifi_rx_he_kind_t;
typedef struct {
    uint16_t guard_interval_ns;
    uint8_t bandwidth_mhz, mcs, he_ltf_size;
    bool dcm_available, dcm;
    bool bandwidth_available, mcs_available, stbc_available, stbc, fec_available, ldpc;
} esp32_mquickjs_wifi_rx_he_signal_t;

/* Decode copied fixed-SDK SU/MU/TB SIG-A words. Width is nominal PPDU width,
 * not occupied tones, RU size, puncturing map, or receiver capture bandwidth.
 * No HE GI is represented as the HT/VHT short-GI boolean. */
static inline esp32_mquickjs_wifi_rx_he_signal_t esp32_mquickjs_wifi_rx_decode_he_signal(
    esp32_mquickjs_wifi_rx_he_kind_t kind, uint32_t a1, uint16_t a2)
{
    esp32_mquickjs_wifi_rx_he_signal_t result = {0};
    if (kind == ESP32_MQUICKJS_WIFI_RX_HE_SU || kind == ESP32_MQUICKJS_WIFI_RX_HE_ER_SU) {
        uint8_t width = (uint8_t)((a1 >> 19U) & 3U);
        uint8_t mcs = (uint8_t)((a1 >> 3U) & 15U);
        bool er = kind == ESP32_MQUICKJS_WIFI_RX_HE_ER_SU;
        result.bandwidth_available = !er || width <= 1U;
        result.bandwidth_mhz = !er ? (uint8_t)(20U << width) : width <= 1U ? 20U : 0U;
        result.mcs_available = !er ? mcs <= 11U :
            (width == 0U && mcs <= 2U) || (width == 1U && mcs == 0U);
        result.mcs = result.mcs_available ? mcs : 0U;
        result.stbc_available = result.fec_available = true;
        bool raw_dcm = (a1 & (1U << 7U)) != 0U;
        bool raw_stbc = (a2 & (1U << 9U)) != 0U;
        uint8_t gi = (uint8_t)((a1 >> 21U) & 3U);
        bool alternate_gi = raw_dcm && raw_stbc;
        /* Both flags signal the special GI, not two enabled data encodings. */
        result.stbc = raw_stbc && !raw_dcm;
        result.dcm_available = true;
        result.dcm = raw_dcm && !raw_stbc;
        result.he_ltf_size = gi == 0U ? 1U : gi == 3U ? 4U : 2U;
        result.guard_interval_ns = gi < 2U || (gi == 3U && alternate_gi) ? 800U : gi == 2U ? 1600U : 3200U;
        result.ldpc = (a2 & (1U << 7U)) != 0U;
    } else if (kind == ESP32_MQUICKJS_WIFI_RX_HE_MU) {
        uint8_t width = (uint8_t)((a1 >> 15U) & 7U);
        result.bandwidth_available = true;
        result.bandwidth_mhz = width < 4U ? (uint8_t)(20U << width) : width < 6U ? 80U : 160U;
        result.stbc_available = true;
        result.stbc = (a2 & (1U << 12U)) != 0U;
        uint8_t gi = (uint8_t)((a1 >> 23U) & 3U);
        result.he_ltf_size = gi == 0U || gi == 3U ? 4U : 2U;
        result.guard_interval_ns = gi < 2U ? 800U : gi == 2U ? 1600U : 3200U;
        /* SIG-B MCS in SIG-A is not the per-user HE-Data MCS/coding. */
    } else if (kind == ESP32_MQUICKJS_WIFI_RX_HE_TB) {
        result.bandwidth_available = true;
        result.bandwidth_mhz = (uint8_t)(20U << ((a1 >> 24U) & 3U));
        /* Per-user MCS, coding and STBC require the triggering allocation. */
    }
    return result;
}
