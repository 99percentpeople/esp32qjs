
int main(void){
    setup();wifi_radio_operation_lock();native_twt_policy=(wifi_twt_config_t){true,false};
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->twt_policy.post_wakeup_event);
    new_driver();assert(!s_twt_policy.known);
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(native_twt_policy.post_wakeup_event && !native_twt_policy.twt_enable_keep_alive && s_twt_policy.known);
    unsigned before=calls;assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && calls==before);
    assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();return 0;
}
