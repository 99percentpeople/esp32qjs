#include "esp32_mquickjs_wifi_csi_target.h"
#include "esp32_mquickjs_wifi_rx_vht_signal.h"
#include "esp32_mquickjs_wifi_rx_he_signal.h"

#include <stddef.h>

esp32_mquickjs_wifi_csi_target_config_result_t
esp32_mquickjs_wifi_csi_target_config_validate(
    bool he_target,
    bool vht_supported,
    bool lltf_bit_mode_supported,
    const esp32_mquickjs_wifi_csi_capture_config_t *config)
{
    if (config == NULL) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID;
    }
    if ((he_target && config->schema != ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE) ||
        (!he_target &&
         config->schema != ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY)) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_SCHEMA_MISMATCH;
    }
    if (!he_target) {
        return config->config.legacy.shift_bits <= 15U
            ? ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK
            : ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID;
    }
    if (config->config.he.he_stbc_ltf >
            ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE ||
        config->config.he.value_scale > 8U ||
        (config->config.he.lltf_bits != 8U &&
         config->config.he.lltf_bits != 12U)) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID;
    }
    if (config->config.he.vht && !vht_supported) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED;
    }
    if (config->config.he.lltf_bits != 12U &&
        !lltf_bit_mode_supported) {
        return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED;
    }
    return ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK;
}

bool esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
    bool supports_5ghz, uint8_t channel)
{
    static const uint8_t channels_5ghz[] = {
        36U, 40U, 44U, 48U, 52U, 56U, 60U, 64U,
        100U, 104U, 108U, 112U, 116U, 120U, 124U, 128U,
        132U, 136U, 140U, 144U, 149U, 153U, 157U, 161U,
        165U, 169U, 173U, 177U,
    };
    size_t index;

    if (channel >= 1U && channel <= 14U) return true;
    if (!supports_5ghz) return false;
    for (index = 0; index < sizeof(channels_5ghz); ++index) {
        if (channels_5ghz[index] == channel) return true;
    }
    return false;
}

void esp32_mquickjs_wifi_csi_target_decode_he_signal(
    esp32_mquickjs_wifi_csi_metadata_t *metadata,
    uint32_t signal_a1,
    uint16_t signal_a2, bool vht_multi_user)
{
    if (metadata == NULL) return;
    metadata->guard_interval_ns = 0;
    metadata->he_ltf_size = metadata->dcm_state = 0;
    metadata->phy_flags = 0;
    metadata->ampdu_count_available = false;
    metadata->mcs_available = metadata->bandwidth_available = metadata->stbc_available = false;
#define PHY_BOOL(value, name, available) do { \
    metadata->phy_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_##available; \
    if (value) metadata->phy_flags |= ESP32_MQUICKJS_WIFI_RX_WIRE_##name; \
} while (0)
    if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HT) {
        /* esp_wifi_htsig_t in ESP-IDF's HE receive metadata. */
        metadata->guard_interval_ns = (signal_a1 & (1UL << 31U)) ? 400U : 800U;
        metadata->mcs = (uint8_t)(signal_a1 & 0x7fU);
        metadata->mcs_available = metadata->mcs <= 76;
        metadata->bandwidth_mhz = (signal_a1 & (1UL << 7U)) != 0U
            ? 40U : 20U;
        metadata->bandwidth_available = true;
        metadata->stbc = ((signal_a1 >> 28U) & 0x3U) != 0U;
        metadata->stbc_available = true;
        PHY_BOOL(signal_a1 & (1UL << 31), SGI, SGI_AVAILABLE);
        PHY_BOOL(signal_a1 & (1UL << 30), LDPC, FEC_AVAILABLE);
        PHY_BOOL(signal_a1 & (1UL << 27), AGGREGATION, AGGREGATION_AVAILABLE);
        PHY_BOOL(signal_a1 & (1UL << 24), SMOOTHING, SMOOTHING_AVAILABLE);
        PHY_BOOL(!(signal_a1 & (1UL << 25)), SOUNDING, SOUNDING_AVAILABLE);
    } else if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_VHT) {
        esp32_mquickjs_wifi_rx_vht_signal_t vht =
            esp32_mquickjs_wifi_rx_decode_vht_signal(signal_a1, vht_multi_user);
        metadata->guard_interval_ns = vht.short_gi ? 400U : 800U;
        metadata->mcs = vht.mcs;
        metadata->mcs_available = vht.mcs_available;
        metadata->bandwidth_mhz = vht.bandwidth_mhz;
        metadata->bandwidth_available = true;
        PHY_BOOL(vht.short_gi, SGI, SGI_AVAILABLE);
        if (vht.fec_available) { PHY_BOOL(vht.ldpc, LDPC, FEC_AVAILABLE); }
        metadata->stbc = vht.stbc;
        metadata->stbc_available = true;
    } else if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU ||
               metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_MU ||
               metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_ER_SU ||
               metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_TB) {
        esp32_mquickjs_wifi_rx_he_kind_t kind =
            metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU ? ESP32_MQUICKJS_WIFI_RX_HE_SU :
            metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_MU ? ESP32_MQUICKJS_WIFI_RX_HE_MU :
            metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_ER_SU ? ESP32_MQUICKJS_WIFI_RX_HE_ER_SU :
            ESP32_MQUICKJS_WIFI_RX_HE_TB;
        esp32_mquickjs_wifi_rx_he_signal_t he =
            esp32_mquickjs_wifi_rx_decode_he_signal(kind, signal_a1, signal_a2);
        metadata->guard_interval_ns = he.guard_interval_ns;
        metadata->he_ltf_size = he.he_ltf_size;
        metadata->dcm_state = he.dcm_available ? (he.dcm ? 2U : 1U) : 0U;
        metadata->mcs = he.mcs;
        metadata->mcs_available = he.mcs_available;
        metadata->bandwidth_mhz = he.bandwidth_mhz;
        metadata->bandwidth_available = he.bandwidth_available;
        if (he.fec_available) { PHY_BOOL(he.ldpc, LDPC, FEC_AVAILABLE); }
        metadata->stbc = he.stbc;
        metadata->stbc_available = he.stbc_available;
    } else if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY) {
        metadata->bandwidth_mhz = 20;
        metadata->bandwidth_available = true;
    }
#undef PHY_BOOL
}
