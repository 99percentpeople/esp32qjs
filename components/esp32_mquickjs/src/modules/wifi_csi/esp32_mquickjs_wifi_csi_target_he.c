#include "esp32_mquickjs_wifi_csi_target.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI && CONFIG_SOC_WIFI_HE_SUPPORT

#include "esp_wifi_he_types.h"

#include <string.h>

bool esp32_mquickjs_wifi_csi_target_is_he(void)
{
    return true;
}

bool esp32_mquickjs_wifi_csi_target_supports_vht(void)
{
#if CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3
    return true;
#else
    return false;
#endif
}

bool esp32_mquickjs_wifi_csi_target_supports_lltf_bit_mode(void)
{
#if CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3
    return true;
#else
    return false;
#endif
}

const char *esp32_mquickjs_wifi_csi_target_schema_name(void)
{
    return "wifi-csi-he/1";
}

esp_err_t esp32_mquickjs_wifi_csi_target_apply_config(
    const esp32_mquickjs_wifi_csi_capture_config_t *config)
{
    wifi_csi_config_t native = {0};

    if (esp32_mquickjs_wifi_csi_target_config_validate(
            true, esp32_mquickjs_wifi_csi_target_supports_vht(),
            esp32_mquickjs_wifi_csi_target_supports_lltf_bit_mode(),
            config) != ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    native.enable = config->config.he.enable;
    native.acquire_csi_legacy = config->config.he.enable_legacy;
#if CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3
    native.acquire_csi_force_lltf = config->config.he.force_legacy_ltf;
#endif
    native.acquire_csi_ht20 = config->config.he.ht20;
    native.acquire_csi_ht40 = config->config.he.ht40;
#if CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3
    native.acquire_csi_vht = config->config.he.vht;
#endif
    native.acquire_csi_su = config->config.he.he_su;
    native.acquire_csi_mu = config->config.he.he_mu;
    native.acquire_csi_dcm = config->config.he.he_dcm;
    native.acquire_csi_beamformed = config->config.he.he_beamformed;
#if CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3
    native.acquire_csi_he_stbc_mode = config->config.he.he_stbc_ltf;
#else
    native.acquire_csi_he_stbc = config->config.he.he_stbc_ltf;
#endif
    native.val_scale_cfg = config->config.he.value_scale;
    native.dump_ack_en = config->config.he.dump_ack;
#if CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3
    native.lltf_bit_mode = config->config.he.lltf_bits == 8U ? 1U : 0U;
#endif
    return esp_wifi_set_csi_config(&native);
}

static esp32_mquickjs_wifi_csi_phy_t he_phy(uint8_t value)
{
    switch (value) {
        case RX_BB_FORMAT_11B:
        case RX_BB_FORMAT_11G:
            return ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY;
        case RX_BB_FORMAT_HT:
            return ESP32_MQUICKJS_WIFI_CSI_PHY_HT;
        case RX_BB_FORMAT_VHT:
        case RX_BB_FORMAT_VHT_MU:
            return ESP32_MQUICKJS_WIFI_CSI_PHY_VHT;
        case RX_BB_FORMAT_HE_SU:
            return ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU;
        case RX_BB_FORMAT_HE_MU:
            return ESP32_MQUICKJS_WIFI_CSI_PHY_HE_MU;
        case RX_BB_FORMAT_HE_ERSU:
            return ESP32_MQUICKJS_WIFI_CSI_PHY_HE_ER_SU;
        case RX_BB_FORMAT_HE_TB:
            return ESP32_MQUICKJS_WIFI_CSI_PHY_HE_TB;
        default:
            return ESP32_MQUICKJS_WIFI_CSI_PHY_UNKNOWN;
    }
}

static esp32_mquickjs_wifi_csi_secondary_t he_secondary(uint8_t value)
{
    return value == 1U ? ESP32_MQUICKJS_WIFI_CSI_SECONDARY_ABOVE :
           value == 2U ? ESP32_MQUICKJS_WIFI_CSI_SECONDARY_BELOW :
                         ESP32_MQUICKJS_WIFI_CSI_SECONDARY_NONE;
}

void esp32_mquickjs_wifi_csi_target_normalize_metadata(
    const wifi_csi_info_t *info,
    const esp32_mquickjs_wifi_csi_capture_config_t *config,
    esp32_mquickjs_wifi_csi_metadata_t *metadata)
{
    const wifi_pkt_rx_ctrl_t *rx = &info->rx_ctrl;

    memset(metadata, 0, sizeof(*metadata));
    memcpy(metadata->source_mac, info->mac, 6U);
    memcpy(metadata->destination_mac, info->dmac, 6U);
    metadata->destination_mac_available = true;
    metadata->rssi = rx->rssi;
    metadata->noise_floor = rx->noise_floor;
    metadata->noise_floor_available = true;
    metadata->channel = rx->channel;
    metadata->secondary = he_secondary(rx->second);
    metadata->timestamp_us = rx->timestamp;
    metadata->rx_sequence = info->rx_seq;
    metadata->first_word_invalid = info->first_word_invalid;
    metadata->channel_estimate_valid =
        rx->rx_channel_estimate_info_vld != 0U;
    metadata->channel_estimate_valid_available = true;
    metadata->phy = he_phy(rx->cur_bb_format);
    metadata->bandwidth_mhz = rx->second == 0U ? 20U : 40U;
    metadata->bandwidth_available = true;
    metadata->sample_bits = config != NULL &&
            config->schema == ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE
        ? config->config.he.lltf_bits
        : 0U;
}

#endif
