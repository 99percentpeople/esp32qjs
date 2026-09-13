
int main(void){
    for(unsigned hidden=0;hidden<2;++hidden){
        setup();wifi_radio_operation_lock();
        native_scan.scan_time.active.max=73;native_scan.scan_time.passive=421;
        assert(wifi_radio_scan_parameters_observe_locked(NULL)==ESP_OK);
        wifi_mode_t mode=hidden?WIFI_MODE_AP:WIFI_MODE_STA;
        s_radio.effective_mode=native_mode=mode;
        assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
        assert(s_config_restart.snapshot->scan_parameters_known);
        assert(s_config_restart.snapshot->scan_parameters.scan_time.active.max==73);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_pre_start_locked(&token,mode)==ESP_OK);
        s_radio.started=s_radio.stop_required=native_running=true;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_commit_locked(&token,mode)==ESP_OK);
        assert(s_scan_parameters.known && s_scan_parameters.value.scan_time.active.max==73);
        if(hidden){assert(native_scan.scan_time.active.max==120);
            s_radio.effective_mode=native_mode=WIFI_MODE_STA;
            assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==ESP_OK);}
        assert(native_scan.scan_time.active.max==73 && native_scan.scan_time.passive==421);
        wipe();wifi_radio_operation_unlock();
    }
    return 0;
}
