
int main(void){
    esp32_mquickjs_wifi_csi_metadata_t m;
    esp32_mquickjs_wifi_csi_capture_config_t config={0};
    config.schema=TEST_HE?ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE:ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY;
    if(TEST_HE){config.config.he.enable=true;config.config.he.enable_legacy=true;config.config.he.ht20=true;config.config.he.lltf_bits=8;}
    else config.config.legacy.lltf=true;
    wifi_csi_info_t info={.len=128,.rx_ctrl={.rssi=-45,.noise_floor=-95,.channel=6,.sig_mode=1,
        .mcs=5,.cwb=1,.stbc=1,.sgi=1,.fec_coding=1,.aggregation=1,.smoothing=1,.ampdu_cnt=7,
        .cur_bb_format=RX_BB_FORMAT_HT,.he_siga1=5U|(1U<<7)|(1U<<24)|(1U<<27)|(1U<<28)|(1U<<30)|(1U<<31)}};
    memset(&m,0xff,sizeof(m));
    esp32_mquickjs_wifi_csi_target_normalize_metadata(&info,&config,&m);
    assert(m.phy==ESP32_MQUICKJS_WIFI_CSI_PHY_HT&&m.mcs_available&&m.mcs==5);
    assert(m.bandwidth_available&&m.bandwidth_mhz==40&&m.guard_interval_ns==400);
    assert(m.address_mask==0&&m.rx_sequence==UINT32_MAX&&!m.frame_subtype_available);
    assert(m.frame_type==ESP32_MQUICKJS_WIFI_PACKET_UNKNOWN);
    assert(m.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC);
    assert(m.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_SOUNDING);
    assert(m.phy_flags & ESP32_MQUICKJS_WIFI_RX_WIRE_AGGREGATION);
    assert(m.antenna_available==!TEST_HE&&m.ampdu_count_available==!TEST_HE);
    info.rx_ctrl.mcs=127;info.rx_ctrl.he_siga1=(info.rx_ctrl.he_siga1&~127U)|127U;
    esp32_mquickjs_wifi_csi_target_normalize_metadata(&info,&config,&m);assert(!m.mcs_available);
    info.rx_ctrl.sig_mode=2;info.rx_ctrl.cur_bb_format=255;
    info.rx_ctrl.second=info.rx_ctrl.secondary_channel=3;
    esp32_mquickjs_wifi_csi_target_normalize_metadata(&info,&config,&m);
    assert(m.phy==ESP32_MQUICKJS_WIFI_CSI_PHY_UNKNOWN&&!m.mcs_available&&!m.bandwidth_available);
    assert((unsigned)m.secondary>2&&!m.layout.known);
    assert(!m.phy_flags&&!m.ampdu_count_available&&m.layout.byte_length==128);
    info.rx_ctrl.sig_mode=0;info.rx_ctrl.cur_bb_format=RX_BB_FORMAT_11G;
    info.rx_ctrl.second=info.rx_ctrl.secondary_channel=0;
    esp32_mquickjs_wifi_csi_target_normalize_metadata(&info,&config,&m);
    assert(m.phy==ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY&&m.bandwidth_available&&m.bandwidth_mhz==20);
    assert(!m.phy_flags&&!m.mcs_available&&!m.ampdu_count_available);
}
