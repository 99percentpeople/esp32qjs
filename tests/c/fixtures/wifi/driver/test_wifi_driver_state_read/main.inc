
int main(void) {
    esp32_mquickjs_wifi_driver_state_readback_t out,zero={0};const char *stage;
#define READ(q) esp32_mquickjs_wifi_radio_read_state(ESP32_MQUICKJS_WIFI_DRIVER_STATE_##q,&out,&stage)
    for(unsigned q=0;q<4;++q) {
        reset();assert(esp32_mquickjs_wifi_radio_read_state(q,&out,&stage)==ESP_OK && !stage && reads==1);
        sdk_error=89;memset(&out,0xff,sizeof(out));
        assert(esp32_mquickjs_wifi_radio_read_state(q,&out,&stage)==89 && !memcmp(&out,&zero,sizeof(out)) && stage);
    }
    reset();overlap=true;assert(READ(CHANNEL)==ESP_OK && out.channel==11 && current_reads==2 && out.channel_generation==1);
    uint32_t generation=out.channel_generation;uint64_t revision=s_radio.channel_observation_revision;
    assert(READ(HOME_CHANNEL)==ESP_OK && out.channel==1 && !out.channel_generation && home_reads==1);
    assert(s_radio.primary_channel==11 && s_radio.channel_generation==generation && s_radio.channel_observation_revision==revision);
    reset();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(READ(MODE)==ESP_OK && READ(COUNTRY)==ESP_OK && READ(CHANNEL)==ESP_OK);
    unsigned before=reads;assert(READ(HOME_CHANNEL)==ESP_ERR_WIFI_NOT_STARTED && reads==before);
    s_radio.operation.identity=1;assert(READ(MODE)==ESP_ERR_INVALID_STATE && reads==before);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(READ(COUNTRY)==ESP_ERR_INVALID_STATE && reads==before);s_radio.lifecycle.identity=0;
    s_radio.restart_required=true;assert(READ(CHANNEL)==ESP_ERR_INVALID_STATE && reads==before);s_radio.restart_required=false;
    s_radio.driver_owned=false;assert(READ(MODE)==ESP_ERR_WIFI_NOT_INIT && reads==before);
    reset();native_mode=WIFI_MODE_NULL;assert(READ(MODE)==ESP_OK && out.mode==WIFI_MODE_NULL);
    native_mode=99;assert(READ(MODE)==ESP_ERR_INVALID_RESPONSE && !strcmp(stage,"decode"));
    native_mode=WIFI_MODE_AP;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(READ(MODE)==ESP_OK);
#else
    assert(READ(MODE)==ESP_ERR_INVALID_RESPONSE);
#endif
    reset();current_channel=home_channel=36;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(READ(CHANNEL)==ESP_OK && READ(HOME_CHANNEL)==ESP_OK);
#else
    assert(READ(CHANNEL)==ESP_ERR_INVALID_RESPONSE && READ(HOME_CHANNEL)==ESP_ERR_INVALID_RESPONSE);
#endif
    reset();second=99;assert(READ(CHANNEL)==ESP_ERR_INVALID_RESPONSE && READ(HOME_CHANNEL)==ESP_ERR_INVALID_RESPONSE);
    reset();country.cc[0]=0;assert(READ(COUNTRY)==ESP_ERR_INVALID_RESPONSE);
    reset();country.policy=99;assert(READ(COUNTRY)==ESP_ERR_INVALID_RESPONSE);
    reset();country.schan=14;country.nchan=2;assert(READ(COUNTRY)==ESP_ERR_INVALID_RESPONSE);
    reset();country.cc[0]='0';country.cc[1]='1';country.cc[2]='X';assert(READ(COUNTRY)==ESP_OK);
    assert(!locks && !critical);return 0;
}
