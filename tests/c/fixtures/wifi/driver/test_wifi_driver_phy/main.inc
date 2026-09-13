
int main(void) {
    esp32_mquickjs_wifi_phy_readback_t out,zero={0};const char *stage;
#define QUERY(kind) esp32_mquickjs_wifi_radio_read_phy(WIFI_IF_STA,ESP32_MQUICKJS_WIFI_PHY_##kind,&out,&stage)
    assert(QUERY(PROTOCOL)==ESP_OK && out.protocols.ghz_2g==bits && !stage && !locks);
    assert(QUERY(BANDWIDTH)==ESP_OK && out.bandwidths.ghz_2g==WIFI_BW40);
    assert(QUERY(PROTOCOLS)==ESP_OK && out.bands==1 && out.protocols.ghz_5g==0);
    assert(QUERY(BANDWIDTHS)==ESP_OK && out.bands==1 && out.bandwidths.ghz_5g==0);
    band_mode=WIFI_BAND_MODE_AUTO;
    assert(QUERY(PROTOCOL)==ESP_ERR_NOT_SUPPORTED && !memcmp(&out,&zero,sizeof(out)));
    assert(QUERY(BANDWIDTH)==ESP_ERR_NOT_SUPPORTED);
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(QUERY(PROTOCOLS)==ESP_OK && out.bands==3 && out.protocols.ghz_5g==(WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N));
    assert(QUERY(BANDWIDTHS)==ESP_OK && out.bands==3 && out.bandwidths.ghz_2g==WIFI_BW40 && out.bandwidths.ghz_5g==WIFI_BW20);
    band_mode=WIFI_BAND_MODE_5G_ONLY;
    assert(QUERY(PROTOCOLS)==ESP_OK && out.bands==2 && !out.protocols.ghz_2g);
    assert(QUERY(BANDWIDTHS)==ESP_OK && out.bands==2 && !out.bandwidths.ghz_2g);
    band_mode=99;
    assert(QUERY(PROTOCOLS)==ESP_ERR_INVALID_RESPONSE && !strcmp(stage,"band-mode"));
#endif
    band_mode=WIFI_BAND_MODE_2G_ONLY;
    sdk_error=89;assert(QUERY(PROTOCOL)==89 && !strcmp(stage,"protocol") && !memcmp(&out,&zero,sizeof(out)));
    assert(QUERY(BANDWIDTHS)==89 && !memcmp(&out,&zero,sizeof(out)));sdk_error=0;
    bits=0x80;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_RESPONSE && !strcmp(stage,"decode"));bits=WIFI_PROTOCOL_11B;
    width=99;assert(QUERY(BANDWIDTH)==ESP_ERR_INVALID_RESPONSE);width=WIFI_BW20;
    unsigned before=reads;
    s_radio.driver_owned=false;assert(QUERY(PROTOCOL)==ESP_ERR_WIFI_NOT_INIT);s_radio.driver_owned=true;
    s_radio.lifecycle.identity=1;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.lifecycle.identity=0;
    s_radio.operation.identity=1;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.fault_stage="fault";assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.fault_stage=NULL;
    s_radio.cleanup_stage="cleanup";assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.cleanup_stage=NULL;
    s_radio.restart_required=true;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.restart_required=false;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING;assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;s_radio.effective_mode=WIFI_MODE_AP;
    assert(QUERY(PROTOCOL)==ESP_ERR_INVALID_STATE);s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(esp32_mquickjs_wifi_radio_read_phy(99,ESP32_MQUICKJS_WIFI_PHY_PROTOCOL,&out,&stage)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_read_phy(WIFI_IF_STA,99,&out,&stage)==ESP_ERR_INVALID_ARG);
    assert(reads==before && !locks);
    int err=esp32_mquickjs_wifi_radio_read_phy(WIFI_IF_AP,ESP32_MQUICKJS_WIFI_PHY_PROTOCOL,&out,&stage);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(err==ESP_OK && reads==before+1);
#else
    assert(err==ESP_ERR_NOT_SUPPORTED && reads==before);
#endif
    return 0;
}
