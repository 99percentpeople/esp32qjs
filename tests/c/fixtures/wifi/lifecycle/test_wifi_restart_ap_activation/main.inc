
static void prepare(wifi_mode_t mode,bool five) {
    setup();wifi_radio_operation_lock();ticks=waits=delays=station_activations=0;wait_limit=8;
    csa_pending=cancel_wait=never_settle=station_channel_drift=false;
    native_mode=s_radio.effective_mode=mode;native_config[1].ap.channel=1;
    native_channel=native_home=five?36:6;native_band=five?WIFI_BAND_5G:WIFI_BAND_2G;
    assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,mode)==ESP_OK);
}
static esp32_mquickjs_wifi_radio_lease_t stage_owner(void) {
    esp32_mquickjs_wifi_radio_lease_t lease={.generation=s_radio.generation,.identity=88,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,.acquired=true};
    s_radio.leases[0]=(wifi_radio_live_lease_t){lease.identity,lease.client};return lease;
}
int main(void) {
    for(unsigned both=0;both<2;++both)for(unsigned five=0;five<(CONFIG_SOC_WIFI_SUPPORT_5G?2U:1U);++five) {
        wifi_mode_t mode=both?WIFI_MODE_APSTA:WIFI_MODE_AP;
        unsigned total=0;
        for(unsigned failure=0;failure<=total;++failure) {
            prepare(mode,five);
            wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
            esp32_mquickjs_wifi_radio_lease_t lease=stage_owner();
            calls=writes=0;fail_at=failure;
            int err=wifi_radio_restart_configs_start_locked(&token,mode,&lease);
            if(!failure) {
                assert(err==ESP_OK);total=calls;
                assert(delays==2 && channel_writes==1 && station_activations==both);
                assert(native_channel==frozen.primary && native_home==frozen.primary && native_mode==mode);
                assert(native_config[1].ap.channel==1 && native_storage==WIFI_STORAGE_RAM && !nvs_writes);
                assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
                assert(wifi_radio_restart_configs_commit_locked(&token,mode)==ESP_OK && native_storage==WIFI_STORAGE_FLASH);
            } else {
                assert(err==77 && allocation && !frees && s_radio.fault_stage);
                unsigned before=calls,start_count=native_starts,channels=channel_writes;
                assert(wifi_radio_restart_configs_start_locked(&token,mode,&lease)==ESP_ERR_INVALID_STATE);
                assert(calls==before && native_starts==start_count && channel_writes==channels);
            }
            assert(!waits && !critical && token.identity==41 && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
            memset(s_radio.leases,0,sizeof(s_radio.leases));wipe();wifi_radio_operation_unlock();
        }
    }
    for(unsigned scenario=0;scenario<5;++scenario) {
        prepare(WIFI_MODE_APSTA,false);
        esp32_mquickjs_wifi_radio_lease_t lease=stage_owner();
        if(scenario==0)never_settle=true;
        if(scenario==1)cancel_wait=true;
        if(scenario==2)wait_limit=0;
        if(scenario==3)station_channel_drift=true;
        esp32_mquickjs_wifi_radio_lifecycle_t request=token;
        if(scenario==4)request.identity++;
        unsigned before=calls;
        int err=wifi_radio_restart_configs_start_locked(&request,WIFI_MODE_APSTA,&lease);
        assert(err==(scenario==1 || scenario==4?ESP_ERR_INVALID_STATE:scenario==3?ESP_ERR_INVALID_RESPONSE:ESP_ERR_TIMEOUT));
        assert(!waits && allocation && !frees && native_storage==WIFI_STORAGE_RAM);
        if(scenario==4)assert(calls==before && !s_radio.started);
        if(scenario<3)assert(!station_activations && channel_writes==1);
        if(scenario==3)assert(station_activations==1);
        memset(s_radio.leases,0,sizeof(s_radio.leases));wipe();wifi_radio_operation_unlock();
    }
    return 0;
}
