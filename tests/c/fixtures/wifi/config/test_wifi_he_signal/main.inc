
int main(void) {
    const uint8_t raw_formats[]={RX_BB_FORMAT_HE_SU,RX_BB_FORMAT_HE_MU,RX_BB_FORMAT_HE_ERSU,RX_BB_FORMAT_HE_TB};
    const uint8_t expected_mu_width[]={20,40,80,160,80,80,160,160};
    for(unsigned kind=0;kind<4;kind++) for(unsigned width=0;width<(kind==1?8U:4U);width++)
    for(unsigned mcs=0;mcs<16;mcs++) for(unsigned flags=0;flags<8;flags++)
    for(unsigned gi=0;gi<4;gi++) {
        uint32_t a1;uint16_t a2;
        if(kind==0 || kind==2) {
            esp_wifi_su_siga1_t one={.bw=width,.he_mcs=mcs,.dcm=!!(flags&4),.gi_and_he_ltf_size=gi};
            esp_wifi_su_siga2_t two={.coding=!!(flags&1),.stbc=!!(flags&2)};
            _Static_assert(sizeof(one)==4 && sizeof(two)==2,"SDK SU SIG storage");
            memcpy(&a1,&one,4);memcpy(&a2,&two,2);
        } else if(kind==1) {
            esp_wifi_mu_siga1_t one={.bw=width,.sigb_mcs=mcs&7,.gi_and_he_ltf_size=gi};
            esp_wifi_mu_siga2_t two={.stbc=!!(flags&2)};
            _Static_assert(sizeof(one)==4 && sizeof(two)==2,"SDK MU SIG storage");
            memcpy(&a1,&one,4);memcpy(&a2,&two,2);
        } else {
            esp_wifi_tb_siga1_t one={.bw=width};
            _Static_assert(sizeof(one)==4,"SDK TB SIG storage");
            memcpy(&a1,&one,4);a2=UINT16_MAX; /* must not decode SU/MU fields here */
        }
        bool bw_known=kind!=2 || width<=1;
        unsigned bw=kind==1?expected_mu_width[width]:kind==2?(width<=1?20:0):20U<<width;
        bool mc_known=kind==0?mcs<=11:kind==2?((width==0&&mcs<=2)||(width==1&&mcs==0)):false;
        esp32_mquickjs_wifi_csi_metadata_t csi={.phy=(esp32_mquickjs_wifi_csi_phy_t)(3+kind),
            .bandwidth_available=true,.mcs_available=true,.stbc_available=true,.mcs=7,.stbc=true};
        esp32_mquickjs_wifi_csi_target_decode_he_signal(&csi,a1,a2,false);
        assert(csi.bandwidth_available==bw_known && csi.bandwidth_mhz==bw);
        assert(csi.mcs_available==mc_known && csi.mcs==(mc_known?mcs:0));
        bool su=kind==0 || kind==2;
        bool stbc=kind!=3 && !!(flags&2) && (!su || !(flags&4));
        static const unsigned su_ltf[]={1,2,2,4}, mu_ltf[]={4,2,2,4}, guard[]={800,800,1600,3200};
        unsigned expected_gi=kind==3?0:su && gi==3 && (flags&6)==6?800:guard[gi];
        unsigned expected_ltf=kind==3?0:su?su_ltf[gi]:mu_ltf[gi];
        assert(csi.stbc_available==(kind!=3) && csi.stbc==stbc);
        assert(csi.guard_interval_ns==expected_gi && csi.he_ltf_size==expected_ltf);
        assert(csi.dcm_state==(su?((flags&4) && !(flags&2)?2:1):0));
        esp32_mquickjs_wifi_monitor_info_t info={.metadata_only=true,.callback_time_us=101,
            .driver={.available=true,.he_layout=true,.type=ESP32_MQUICKJS_WIFI_PACKET_MISC,
                .raw_format=raw_formats[kind],.signal_word1=a1,.signal_word2=a2}};
        esp32_mquickjs_wifi_monitor_wire_snapshot_t out;
        assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,1,2,3,&out));
        assert(out.metadata.phy==3+kind && out.metadata.bandwidth_mhz==bw);
        assert(out.metadata.mcs==(mc_known?mcs:UINT8_MAX));
        assert(out.metadata.ampdu_count==UINT8_MAX && out.metadata.antenna==UINT8_MAX);
        uint32_t r=out.metadata.rx_flags;
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_MCS_AVAILABLE)==mc_known);
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_BANDWIDTH_AVAILABLE)==bw_known);
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_STBC_AVAILABLE)==(kind!=3));
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_FEC_AVAILABLE)==(kind==0 || kind==2));
        assert(!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_SGI_AVAILABLE));
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_STBC)==stbc);
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_DCM_AVAILABLE)==su);
        assert(!!(r&ESP32_MQUICKJS_WIFI_RX_WIRE_DCM)==(csi.dcm_state==2));
        assert(out.metadata.guard_interval_ns==expected_gi && out.metadata.he_ltf_size==expected_ltf);
        uint8_t bytes[256];
        assert(esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,
            &out.frame,&out.metadata,bytes,sizeof(bytes)));
        assert(bytes[62]==bw && bytes[63]==(mc_known?mcs:255));
        assert((unsigned)(bytes[124]|(bytes[125]<<8))==expected_gi && bytes[126]==expected_ltf && !bytes[127]);
    }
    esp32_mquickjs_wifi_rx_he_signal_t unknown=esp32_mquickjs_wifi_rx_decode_he_signal(
        (esp32_mquickjs_wifi_rx_he_kind_t)99,UINT32_MAX,UINT16_MAX);
    assert(!unknown.guard_interval_ns && !unknown.he_ltf_size && !unknown.dcm_available);
    assert(!unknown.mcs_available && !unknown.bandwidth_available && !unknown.stbc_available && !unknown.fec_available);
}
