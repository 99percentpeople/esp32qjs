
int main(void) {
    for(unsigned mode_index=0;mode_index<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?3U:1U);++mode_index) {
        wifi_mode_t mode=mode_index==0?WIFI_MODE_STA:mode_index==1?WIFI_MODE_AP:WIFI_MODE_APSTA;
        for(unsigned ram=0;ram<2;++ram) {
            unsigned total=0;
            for(unsigned failure=0;failure<=total;++failure) {
                setup();wifi_radio_operation_lock();
                native_mode=s_radio.effective_mode=mode;
                native_config[1].ap.channel=native_channel;
                if(ram)s_radio.storage=native_storage=WIFI_STORAGE_RAM;
                assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
                wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
                new_driver();
                assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
                assert(s_radio.storage_configured && s_radio.storage==WIFI_STORAGE_RAM && native_storage==WIFI_STORAGE_RAM);
                assert(wifi_radio_restart_configs_pre_start_locked(&token,mode)==ESP_OK);
                assert(esp_wifi_start()==ESP_OK && !nvs_writes);
                s_radio.started=s_radio.stop_required=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
                assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
                assert(native_storage==WIFI_STORAGE_RAM && !nvs_writes);
                calls=writes=0;fail_at=failure;
                int err=wifi_radio_restart_configs_commit_locked(&token,mode);
                if(!failure) {
                    assert(err==ESP_OK);total=calls;
                    assert(s_radio.storage_configured && s_radio.storage==frozen.storage && native_storage==frozen.storage);
                    assert(writes==(ram?0U:1U));
                } else {
                    assert(err==77 && allocation && !frees && token.identity==41);
                    assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
                    if(!ram && failure==total) {
                        assert(!s_radio.storage_configured && writes==1);
                        assert(!strcmp(s_radio.fault_stage,"restart-storage-commit"));
                        unsigned before=calls;
                        assert(wifi_radio_restart_configs_commit_locked(&token,mode)==ESP_ERR_INVALID_STATE && calls==before);
                        assert(!strcmp(s_radio.fault_stage,"restart-storage-commit"));
                    } else assert(native_storage==WIFI_STORAGE_RAM && !writes);
                }
                assert(!nvs_writes);
                wipe();wifi_radio_operation_unlock();
            }
        }
    }
    /* A foreign policy before START is rejected before credentials/readbacks. */
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.storage=native_storage=WIFI_STORAGE_FLASH;unsigned before=calls;
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before && !s_radio.started);
    wipe();wifi_radio_operation_unlock();
    return 0;
}
