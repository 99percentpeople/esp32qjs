
static void source(wifi_mode_t mode,wifi_storage_t storage) {
    setup();s_radio.started=s_radio.stop_required=native_running=false;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.effective_mode=native_mode=mode;s_radio.storage=native_storage=storage;
    wifi_radio_operation_lock();
    assert(!wifi_radio_stop_snapshot_unchanged_locked());
}
static void retire(void) {
    fail_at=0;
    assert(wifi_radio_stop_locked()==ESP_OK);
    wipe();wifi_radio_operation_unlock();
}
int main(void) {
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        const wifi_storage_t stores[]={WIFI_STORAGE_RAM,WIFI_STORAGE_FLASH};
        for(unsigned storage=0;storage<2;++storage) {
            source(mode,stores[storage]);
            assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
            unsigned steps=calls;
            wifi_radio_restart_configs_t *saved=s_config_restart.snapshot;
            assert(saved && s_config_restart.captured && saved->storage==stores[storage]);
            assert(native_starts==1 && !native_stops && s_radio.started && s_radio.stop_required);
            assert(s_radio.storage==WIFI_STORAGE_RAM && native_storage==WIFI_STORAGE_RAM && !nvs_writes);
            assert(saved->saved[0].sta.listen_interval==9 && saved->country.max_tx_power==18 && saved->tx_power==60);
            assert(saved->inactive_mask==mode && saved->primary==1);
            assert(!memcmp(saved->saved[0].sta.password,"secret-sta",10));
            unsigned attempts=calls;
            assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK && calls==attempts);
            retire();
            for(unsigned failure=1;failure<=steps;++failure) {
                source(mode,stores[storage]);fail_at=failure;
                assert(wifi_radio_restart_configs_capture_locked(&token,mode)==77);
                assert(!s_config_restart.captured && !nvs_writes && native_starts<=1);
                assert(s_radio.configuration.error==77 && s_radio.lifecycle.identity==41);
                if(s_radio.configuration.mutation_attempted) {
                    assert(allocation && s_config_restart.snapshot==allocation && !frees);
                    assert(s_config_restart.snapshot->capture_phase==RESTART_CAPTURE_FAILED);
                    assert(s_config_restart.snapshot->capture_error==77 && s_radio.fault_stage);
                    unsigned before=calls;
                    assert(wifi_radio_restart_configs_capture_locked(&token,mode)!=ESP_OK);
                    assert(calls==before && allocation); /* No duplicate START or partial replay. */
                } else assert(!allocation && frees==1 && !native_starts);
                retire();
            }
            source(mode,stores[storage]);allocation_fail=true;
            assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_ERR_NO_MEM);
            assert(!calls && !writes && !native_starts && !allocation);
            wifi_radio_operation_unlock();
            source(mode,stores[storage]);s_interval.known=false;
            assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_ERR_INVALID_STATE);
            assert(!calls && !writes && !native_starts && !allocation && frees==1);
            wifi_radio_operation_unlock();
        }
    }
    for(unsigned pending=0;pending<4;++pending) {
        source(WIFI_MODE_STA,WIFI_STORAGE_RAM);
        if(pending==0)s_radio.stop_required=true;
        if(pending==1)s_radio.stop_submitted=true;
        if(pending==2)s_radio.event_phase=RADIO_EVENTS_START;
        if(pending==3)s_radio.event_live=1;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(!calls && !allocations && !native_starts);
        wifi_radio_operation_unlock();
    }
    assert(!allocation && !locks && !critical && !helper_locks);
    return 0;
}
