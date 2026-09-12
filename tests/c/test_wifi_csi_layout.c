#include "esp32_mquickjs_wifi_csi_target.h"

#include <assert.h>
#include <string.h>

static void test_legacy_layouts(void)
{
    esp32_mquickjs_wifi_csi_capture_config_t config = {0};
    esp32_mquickjs_wifi_csi_metadata_t metadata = {0};

    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY;
    config.config.legacy.lltf = true;
    config.config.legacy.ht_ltf = true;
    config.config.legacy.stbc_ht_ltf2 = true;
    metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_HT;
    metadata.bandwidth_available = true;
    metadata.bandwidth_mhz = 40U;
    metadata.secondary = ESP32_MQUICKJS_WIFI_CSI_SECONDARY_ABOVE;
    metadata.stbc_available = true;
    metadata.stbc = true;

    esp32_mquickjs_wifi_csi_target_build_legacy_layout(
        &metadata, &config, 612U);
    assert(metadata.layout.known);
    assert(metadata.layout.sample_encoding ==
           ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8);
    assert(metadata.layout.iq_pair_count == 306U);
    assert(metadata.layout.segment_count == 3U);
    assert(metadata.layout.segments[1].type ==
           ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HT_LTF);
    assert(metadata.layout.segments[2].type ==
           ESP32_MQUICKJS_WIFI_CSI_SEGMENT_STBC_HT_LTF2);

    config.config.legacy.ht_ltf = false;
    esp32_mquickjs_wifi_csi_target_build_legacy_layout(
        &metadata, &config, 370U);
    assert(metadata.layout.known);
    assert(metadata.layout.segment_count == 2U);
    assert(metadata.layout.segments[1].type ==
           ESP32_MQUICKJS_WIFI_CSI_SEGMENT_STBC_HT_LTF2);
    config.config.legacy.ht_ltf = true;

    esp32_mquickjs_wifi_csi_target_build_legacy_layout(
        &metadata, &config, 611U);
    assert(!metadata.layout.known);
    assert(metadata.layout.segment_count == 1U);
    assert(metadata.layout.segments[0].type ==
           ESP32_MQUICKJS_WIFI_CSI_SEGMENT_UNKNOWN);
}

static void test_he_layouts_and_encodings(void)
{
    esp32_mquickjs_wifi_csi_capture_config_t config = {0};
    esp32_mquickjs_wifi_csi_metadata_t metadata = {0};

    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE;
    config.config.he.enable_legacy = true;
    metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY;

    esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 106U);
    assert(metadata.layout.known && metadata.layout.iq_pair_count == 53U);
    assert(metadata.layout.sample_encoding ==
           ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8);

    esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 160U);
    assert(metadata.layout.known);
    assert(metadata.layout.sample_encoding ==
           ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT12_PACKED);
    assert(metadata.layout.trailing_padding_bytes == 1U);

    esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 212U);
    assert(metadata.layout.known);
    assert(metadata.layout.sample_encoding ==
           ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT12_LE);

    memset(&metadata, 0, sizeof(metadata));
    metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU;
    metadata.mcs_available = true;
    metadata.bandwidth_available = true;
    metadata.bandwidth_mhz = 20U;
    metadata.stbc_available = true;
    metadata.stbc = true;
    config.config.he.he_stbc_ltf = ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE;
    esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 490U);
    assert(metadata.layout.known && metadata.layout.iq_pair_count == 245U);
    assert(metadata.layout.segments[0].type ==
           ESP32_MQUICKJS_WIFI_CSI_SEGMENT_MIXED);
    assert(metadata.layout.segments[0].null_subcarrier_count == 3U);
    assert(metadata.layout.segments[0].subcarrier_ranges[0].start == 0);
    assert(metadata.layout.segments[0].subcarrier_ranges[1].start == -122);

    /* Raw STBC bit in this HE-SIG combination does not request a second LTF. */
    esp32_mquickjs_wifi_csi_target_decode_he_signal(&metadata,
        (1U << 7U) | (3U << 21U), 1U << 9U, false);
    assert(metadata.stbc_available && !metadata.stbc && metadata.dcm_state == 1);
    assert(metadata.guard_interval_ns == 800 && metadata.he_ltf_size == 4);
    esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 490U);
    assert(metadata.layout.known);
    assert(metadata.layout.segments[0].type ==
           ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HE_LTF1);

    metadata.mcs_available = false;
    esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 490U);
    assert(!metadata.layout.known); /* reserved/unavailable SU MCS */

    metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_HE_MU;
    esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 490U);
    assert(!metadata.layout.known);
}

static void test_vht_unknown_layout_sources(void)
{
    esp32_mquickjs_wifi_csi_capture_config_t config = {0};
    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE;
    config.config.he.vht = true;
    config.config.he.lltf_bits = 8;
    for (unsigned width = 0; width < 4; ++width) {
        esp32_mquickjs_wifi_csi_metadata_t metadata = {0};
        metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_VHT;
        metadata.bandwidth_available = metadata.stbc_available = true;
        metadata.bandwidth_mhz = (uint8_t)(20U << width);
        metadata.secondary = ESP32_MQUICKJS_WIFI_CSI_SECONDARY_ABOVE;
        esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 114U);
        assert(!metadata.layout.known); /* no SU MCS */
        metadata.mcs_available = true;
        metadata.mcs = 7;
        if (width >= 2) {
            esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 234U);
            assert(!metadata.layout.known); /* never a false HT40 layout */
        }
    }
}

int main(void)
{
    test_vht_unknown_layout_sources();
    test_legacy_layouts();
    test_he_layouts_and_encodings();
    esp32_mquickjs_wifi_csi_metadata_t metadata = {0};
    esp32_mquickjs_wifi_csi_capture_config_t config = {0};
    metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_UNKNOWN;
    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY;
    config.config.legacy.lltf = true;
    esp32_mquickjs_wifi_csi_target_build_legacy_layout(&metadata, &config, 128);
    assert(!metadata.layout.known && metadata.layout.byte_length == 128);
    metadata.phy = ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY;
    metadata.secondary = (esp32_mquickjs_wifi_csi_secondary_t)3;
    esp32_mquickjs_wifi_csi_target_build_legacy_layout(&metadata, &config, 128);
    assert(!metadata.layout.known && metadata.layout.byte_length == 128);
    config.schema = ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE;
    config.config.he.enable_legacy = true;
    esp32_mquickjs_wifi_csi_target_build_he_layout(&metadata, &config, 106);
    assert(!metadata.layout.known && metadata.layout.byte_length == 106);
    return 0;
}
