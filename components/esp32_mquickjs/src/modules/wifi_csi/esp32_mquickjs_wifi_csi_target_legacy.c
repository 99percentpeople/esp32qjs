#include "esp32_mquickjs_wifi_csi_target.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI && !CONFIG_SOC_WIFI_HE_SUPPORT

#include <string.h>

bool esp32_mquickjs_wifi_csi_target_is_he(void)
{
    return false;
}

bool esp32_mquickjs_wifi_csi_target_supports_vht(void)
{
    return false;
}

bool esp32_mquickjs_wifi_csi_target_supports_lltf_bit_mode(void)
{
    return false;
}

const char *esp32_mquickjs_wifi_csi_target_schema_name(void)
{
    return "wifi-csi-legacy/1";
}

esp_err_t esp32_mquickjs_wifi_csi_target_apply_config(
    const esp32_mquickjs_wifi_csi_capture_config_t *config)
{
    wifi_csi_config_t native = {0};

    if (esp32_mquickjs_wifi_csi_target_config_validate(
            false, false, false, config) !=
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    native.lltf_en = config->config.legacy.lltf;
    native.htltf_en = config->config.legacy.ht_ltf;
    native.stbc_htltf2_en = config->config.legacy.stbc_ht_ltf2;
    native.ltf_merge_en = config->config.legacy.ltf_merge;
    native.channel_filter_en =
        config->config.legacy.adjacent_subcarrier_filter;
    native.manu_scale = config->config.legacy.manual_scale;
    native.shift = config->config.legacy.shift_bits;
    native.dump_ack_en = config->config.legacy.dump_ack;
    return esp_wifi_set_csi_config(&native);
}

static esp32_mquickjs_wifi_csi_secondary_t legacy_secondary(uint8_t value)
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

    (void)config;
    memset(metadata, 0, sizeof(*metadata));
    memcpy(metadata->source_mac, info->mac, 6U);
    memcpy(metadata->destination_mac, info->dmac, 6U);
    metadata->destination_mac_available = true;
    metadata->rssi = rx->rssi;
    metadata->noise_floor = rx->noise_floor;
    metadata->noise_floor_available = true;
    metadata->channel = rx->channel;
    metadata->secondary = legacy_secondary(rx->secondary_channel);
    metadata->timestamp_us = rx->timestamp;
    metadata->rx_sequence = info->rx_seq;
    metadata->antenna = rx->ant;
    metadata->antenna_available = true;
    metadata->first_word_invalid = info->first_word_invalid;
    metadata->sample_bits = 8U;
    metadata->stbc = rx->stbc != 0U;
    metadata->stbc_available = true;
    if (rx->sig_mode == 1U) {
        metadata->phy = ESP32_MQUICKJS_WIFI_CSI_PHY_HT;
        metadata->mcs = rx->mcs;
        metadata->mcs_available = true;
        metadata->bandwidth_mhz = rx->cwb ? 40U : 20U;
        metadata->bandwidth_available = true;
    } else if (rx->sig_mode == 3U) {
        metadata->phy = ESP32_MQUICKJS_WIFI_CSI_PHY_VHT;
        metadata->mcs = rx->mcs;
        metadata->mcs_available = true;
        metadata->bandwidth_mhz = rx->cwb ? 40U : 20U;
        metadata->bandwidth_available = true;
    } else {
        metadata->phy = ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY;
        metadata->bandwidth_mhz = 20U;
        metadata->bandwidth_available = true;
    }
}

#endif
