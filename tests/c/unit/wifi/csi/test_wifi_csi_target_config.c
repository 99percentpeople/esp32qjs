#include "esp32_mquickjs_wifi_csi_target.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    esp32_mquickjs_wifi_csi_capture_config_t config;

    memset(&config, 0, sizeof(config));
    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY;
    config.config.legacy.lltf = true;
    config.config.legacy.ht_ltf = true;
    config.config.legacy.shift_bits = 15;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        false, false, false, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK);
    config.config.legacy.shift_bits = 16;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        false, false, false, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID);

    assert(esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
        false, 1U));
    assert(esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
        false, 14U));
    assert(!esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
        false, 36U));
    assert(esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
        true, 36U));
    assert(esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
        true, 177U));
    assert(!esp32_mquickjs_wifi_csi_target_channel_structurally_valid(
        true, 35U));
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, true, true, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_SCHEMA_MISMATCH);

    memset(&config, 0, sizeof(config));
    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE;
    config.config.he.enable = true;
    config.config.he.vht = true;
    config.config.he.lltf_bits = 12;
    config.config.he.value_scale = 8;
    config.config.he.he_stbc_ltf =
        ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, true, true, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_OK);
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, false, true, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED);
    config.config.he.vht = false;
    config.config.he.lltf_bits = 8;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, true, false, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_UNSUPPORTED);
    config.config.he.lltf_bits = 7;
    assert(esp32_mquickjs_wifi_csi_target_config_validate(
        true, true, true, &config) ==
        ESP32_MQUICKJS_WIFI_CSI_TARGET_CONFIG_INVALID);

    {
        esp32_mquickjs_wifi_csi_metadata_t metadata = {0};

        metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_HT;
        esp32_mquickjs_wifi_csi_target_decode_he_signal(
            &metadata, 5U | (1UL << 7U) | (1UL << 28U), 0U, false);
        assert(metadata.mcs_available && metadata.mcs == 5U);
        assert(metadata.bandwidth_available && metadata.bandwidth_mhz == 40U);
        assert(metadata.stbc_available && metadata.stbc);
        assert(metadata.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_FEC_AVAILABLE);
        assert(!(metadata.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC));
        assert(metadata.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_SOUNDING);
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&metadata,
            127U | (1UL<<31) | (1UL<<30) | (1UL<<27) | (1UL<<25) | (1UL<<24), 0, false);
        assert(!metadata.mcs_available && metadata.guard_interval_ns==400);
        assert(metadata.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_SGI);
        assert(metadata.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC);
        assert(metadata.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_AGGREGATION);
        assert(metadata.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_SMOOTHING);
        assert(!(metadata.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_SOUNDING));
        assert(!metadata.ampdu_count_available);

        memset(&metadata, 0, sizeof(metadata));
        metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_VHT;
        esp32_mquickjs_wifi_csi_target_decode_he_signal(
            &metadata, (9UL << 28U) | (1UL << 3U), 0U, false);
        assert(metadata.mcs == 9U && metadata.bandwidth_mhz == 20U);
        assert(metadata.stbc);

        memset(&metadata, 0, sizeof(metadata));
        metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU;
        esp32_mquickjs_wifi_csi_target_decode_he_signal(
            &metadata, 7UL << 3U, 1U << 9U, false);
        assert(metadata.mcs == 7U && metadata.bandwidth_mhz == 20U);
        assert(metadata.stbc);
        assert(metadata.guard_interval_ns==800 && metadata.he_ltf_size==1 && metadata.dcm_state==1);
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&metadata,
            (1U<<7U)|(3U<<21U), 1U<<9U, false);
        assert(metadata.stbc_available && !metadata.stbc && metadata.dcm_state==1);
        assert(metadata.guard_interval_ns==800 && metadata.he_ltf_size==4);
        metadata.phy=ESP32_MQUICKJS_WIFI_CSI_PHY_HE_TB;
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&metadata,UINT32_MAX,UINT16_MAX,false);
        assert(!metadata.guard_interval_ns && !metadata.he_ltf_size && !metadata.dcm_state);
    }
    /* Production decoder must clear an earlier SU MCS on the MU path. */
    for (unsigned width = 0; width < 4; ++width) {
        esp32_mquickjs_wifi_csi_metadata_t metadata = {0};
        metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_VHT;
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&metadata,
            (9UL << 28U) | (63U << 4U) | width, 0, false);
        assert(metadata.mcs_available && metadata.mcs == 9);
        assert(metadata.bandwidth_available && metadata.bandwidth_mhz == (20U << width));
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&metadata,
            (9UL << 28U) | (63U << 4U) | width, 0, true);
        assert(!metadata.mcs_available && metadata.mcs == 0);
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&metadata,
            (9UL << 28U) | (1U << 4U) | width, 0, false);
        assert(!metadata.mcs_available);
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&metadata,
            (15UL << 28U) | width, 0, false);
        assert(!metadata.mcs_available);
    }
    return 0;
}
