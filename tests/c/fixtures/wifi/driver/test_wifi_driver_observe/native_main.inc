
int main(void) {
    int64_t out;const char *stage;
    for(unsigned i=0;i<=ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME;++i) {
        out=55;assert(esp32_mquickjs_wifi_radio_read_driver(i,WIFI_IF_STA,&out,&stage)==ESP_OK && !stage && !locks);
        sdk_error=89;
        if(i!=ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME) {
            assert(esp32_mquickjs_wifi_radio_read_driver(i,WIFI_IF_STA,&out,&stage)==89 && out==0 && stage && !locks);
        }
        sdk_error=0;
    }
#define QUERY(kind) esp32_mquickjs_wifi_radio_read_driver(ESP32_MQUICKJS_WIFI_DRIVER_##kind,WIFI_IF_STA,&out,&stage)
    assert(QUERY(TX_POWER)==ESP_OK && out==73);
    assert(QUERY(RSSI)==ESP_OK && out==-63);
    aid=0;assert(QUERY(AID)==ESP_OK && out==0);
    tsf=0;assert(QUERY(TSF_TIME)==ESP_OK && out==0);
    tsf=INT64_C(9007199254740991);assert(QUERY(TSF_TIME)==ESP_OK && out==tsf);
    tsf++;assert(QUERY(TSF_TIME)==ESP_ERR_INVALID_RESPONSE && !out && !strcmp(stage,"decode"));
    tsf=-1;assert(QUERY(TSF_TIME)==ESP_ERR_INVALID_RESPONSE);tsf=0;
    band=99;assert(QUERY(BAND)==ESP_ERR_INVALID_RESPONSE);band=WIFI_BAND_5G;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(QUERY(BAND)==ESP_OK);
#else
    assert(QUERY(BAND)==ESP_ERR_INVALID_RESPONSE);
#endif
    band=WIFI_BAND_2G;band_mode=99;assert(QUERY(BAND_MODE)==ESP_ERR_INVALID_RESPONSE);band_mode=WIFI_BAND_MODE_2G_ONLY;
    ps=99;assert(QUERY(POWER_SAVE)==ESP_ERR_INVALID_RESPONSE);ps=WIFI_PS_NONE;
    phy=99;assert(QUERY(NEGOTIATED_PHY)==ESP_ERR_INVALID_RESPONSE);phy=WIFI_PHY_MODE_HT20;
    s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(QUERY(BAND)==ESP_OK && QUERY(AID)==ESP_OK && QUERY(TX_POWER)==ESP_ERR_WIFI_NOT_STARTED);
    assert(QUERY(TSF_TIME)==ESP_ERR_WIFI_NOT_STARTED && QUERY(INACTIVE_TIME)==ESP_ERR_WIFI_NOT_STARTED);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    unsigned before=reads;s_radio.driver_owned=false;assert(QUERY(BAND)==ESP_ERR_WIFI_NOT_INIT);s_radio.driver_owned=true;
    s_radio.operation.identity=1;assert(QUERY(BAND)==ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(QUERY(BAND)==ESP_ERR_INVALID_STATE);s_radio.lifecycle.identity=0;
    s_radio.fault_stage="fault";assert(QUERY(BAND)==ESP_ERR_INVALID_STATE);s_radio.fault_stage=NULL;
    s_radio.cleanup_stage="cleanup";assert(QUERY(BAND)==ESP_ERR_INVALID_STATE);s_radio.cleanup_stage=NULL;
    s_radio.effective_mode=WIFI_MODE_AP;assert(QUERY(RSSI)==ESP_ERR_INVALID_STATE);
    assert(reads==before && !locks);
    int err=esp32_mquickjs_wifi_radio_read_driver(ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME,WIFI_IF_AP,&out,&stage);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(err==ESP_OK && out==300);
#else
    assert(err==ESP_ERR_NOT_SUPPORTED);
#endif
    return 0;
}
