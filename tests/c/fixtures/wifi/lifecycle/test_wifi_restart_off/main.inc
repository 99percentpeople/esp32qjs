
static void source(wifi_storage_t storage) {
    setup();temporary_releases=0;off_readback_corrupt=false;
    s_radio.started=s_radio.stop_required=native_running=false;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.effective_mode=native_mode=WIFI_MODE_NULL;s_radio.storage=native_storage=storage;
    wifi_radio_operation_lock();
}
static void dispose(void) {
    fail_at=0;cleanup_error=0;
    assert(wifi_radio_stop_locked()==ESP_OK);
    wipe();wifi_radio_operation_unlock();
}
int main(void) {
    const wifi_storage_t stores[]={WIFI_STORAGE_RAM,WIFI_STORAGE_FLASH};
    for(unsigned ai=0;ai<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?2U:1U);++ai) {
        wifi_mode_t work=ai?WIFI_MODE_APSTA:WIFI_MODE_STA;
        for(unsigned si=0;si<2;++si) {
            unsigned capture_steps=0;
            for(unsigned failure=0;failure<=capture_steps;++failure) {
                source(stores[si]);fail_at=failure;
                int error=wifi_radio_restart_configs_capture_locked(&token,work);
                if(!failure) {
                    assert(!error);capture_steps=calls;
                    assert(s_config_restart.captured && s_config_restart.snapshot->restore_off);
                    assert(s_config_restart.snapshot->storage==stores[si]);
                    assert(!memcmp(s_config_restart.snapshot->saved[0].sta.password,"secret-sta",10));
                    assert(native_storage==WIFI_STORAGE_RAM && !nvs_writes);
                } else {
                    assert(error==77 && !s_config_restart.captured && !nvs_writes);
                    if(s_radio.configuration.mutation_attempted) {
                        assert(allocation && !frees && s_config_restart.snapshot->restore_off);
                        assert(s_config_restart.snapshot->capture_phase==RESTART_CAPTURE_FAILED);
                        unsigned before=calls;
                        assert(wifi_radio_restart_configs_capture_locked(&token,work)!=ESP_OK && calls==before);
                    } else assert(!allocation && !native_starts);
                }
                dispose();
            }
            unsigned suffix_steps=0;
            for(unsigned failure=0;failure<=suffix_steps;++failure) {
                source(stores[si]);
                assert(!wifi_radio_restart_configs_capture_locked(&token,work));
                assert(!wifi_radio_stop_locked());
                new_driver();assert(!wifi_radio_restart_configs_replay_locked(&token));
                unsigned before=calls;
                assert(wifi_radio_restart_configs_commit_storage_locked(&token)==ESP_ERR_INVALID_STATE);
                assert(calls==before && native_storage==WIFI_STORAGE_RAM);
                calls=writes=0;fail_at=failure;
                wifi_radio_operation_unlock();
                int error=esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(&token,work);
                assert(allocation && !frees && token.identity==41 && s_radio.lifecycle.identity==41);
                assert(!nvs_writes && s_config_restart.snapshot->restore_off);
                if(!failure) {
                    assert(!error);suffix_steps=calls;
                    assert(!s_radio.started && !s_radio.stop_required && !native_running);
                    assert(s_radio.effective_mode==WIFI_MODE_NULL && native_mode==WIFI_MODE_NULL);
                    assert(s_radio.storage_configured && s_radio.storage==stores[si] && native_storage==stores[si]);
                    assert(temporary_releases==1 && s_config_restart.snapshot->storage==stores[si]);
                    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
                    /* Helper retirement is external; only its later finish may wipe the checkpoint. */
                    assert(!esp32_mquickjs_wifi_radio_finish_lifecycle(&token,false));
                    assert(!allocation && frees==1 && !token.identity);
                } else {
                    assert(error==77);
                    wifi_radio_operation_lock();dispose();
                }
            }
        }
    }
    source(WIFI_STORAGE_FLASH);
    assert(!wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA));
    assert(!wifi_radio_stop_locked());
    new_driver();assert(!wifi_radio_restart_configs_replay_locked(&token));
    unsigned before=calls;
    esp32_mquickjs_wifi_radio_lifecycle_t foreign=token;foreign.identity++;
    wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(&foreign,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(calls==before && allocation && token.identity==41);
    off_readback_corrupt=true;
    assert(esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
    assert(native_storage==WIFI_STORAGE_RAM && allocation && token.identity==41 && !nvs_writes);
    assert(!strcmp(s_radio.fault_stage,"restart-off-mode-readback"));
    off_readback_corrupt=false;wifi_radio_operation_lock();dispose();
    assert(!allocation && !locks && !critical && !helper_locks);
    return 0;
}
