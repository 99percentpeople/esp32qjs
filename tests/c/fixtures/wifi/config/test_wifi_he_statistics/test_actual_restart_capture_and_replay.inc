
int main(void){
    setup();wifi_radio_operation_lock();
    native_statistics=(esp32_mquickjs_wifi_he_statistics_t){true,true,9};
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(wifi_he_statistics_equal(&s_config_restart.snapshot->he_statistics,&native_statistics));
    assert(wifi_radio_retire_he_statistics_locked()==ESP_OK && !native_statistics.tx_mask);
    new_driver();memset(&s_he_statistics,0,sizeof(s_he_statistics));
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(native_statistics.ordinary && native_statistics.multi_user && native_statistics.tx_mask==9);
    assert(s_config_restart.he_completed==5);
    unsigned rx_before=rx_writes;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && rx_writes==rx_before);
    assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();
    return 0;
}
