#include "esp32_mquickjs_wifi_csi_target.h"

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
    uint16_t signal_a2)
{
    if (metadata == NULL) return;
    if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HT) {
        /* esp_wifi_htsig_t in ESP-IDF's HE receive metadata. */
        metadata->mcs = (uint8_t)(signal_a1 & 0x7fU);
        metadata->mcs_available = true;
        metadata->bandwidth_mhz = (signal_a1 & (1UL << 7U)) != 0U
            ? 40U : 20U;
        metadata->bandwidth_available = true;
        metadata->stbc = ((signal_a1 >> 28U) & 0x3U) != 0U;
        metadata->stbc_available = true;
    } else if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_VHT) {
        /* esp_wifi_vht_siga1_t: CBW[1:0], STBC[3], SU-MCS[31:28]. */
        metadata->mcs = (uint8_t)((signal_a1 >> 28U) & 0x0fU);
        metadata->mcs_available = true;
        metadata->bandwidth_mhz = (signal_a1 & 0x3U) == 0U ? 20U : 40U;
        metadata->bandwidth_available = true;
        metadata->stbc = (signal_a1 & (1UL << 3U)) != 0U;
        metadata->stbc_available = true;
    } else if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU) {
        /* esp_wifi_su_siga1_t/esp_wifi_su_siga2_t in ESP-IDF. */
        metadata->mcs = (uint8_t)((signal_a1 >> 3U) & 0x0fU);
        metadata->mcs_available = true;
        metadata->bandwidth_available =
            ((signal_a1 >> 19U) & 0x3U) == 0U;
        metadata->bandwidth_mhz = metadata->bandwidth_available ? 20U : 0U;
        metadata->stbc = (signal_a2 & (1U << 9U)) != 0U;
        metadata->stbc_available = true;
    } else if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY) {
        metadata->stbc = false;
        metadata->stbc_available = true;
    }
}
