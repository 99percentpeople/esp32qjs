
int main(void) {
    setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    native_twt_policy=(wifi_twt_config_t){true,false};
    s_twt_policy.value=native_twt_policy;s_twt_policy.known=true;
    cleanup_error=77;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77);
    assert(allocation && !s_config_restart.captured && s_config_restart.snapshot->capture_phase==RESTART_CAPTURE_STOP);
    const char *original_fault=s_radio.fault_stage;
    assert(original_fault && s_radio.fault_error==77 && !policy_writes);
    cleanup_error=0;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.captured && s_radio.fault_stage==original_fault && s_radio.fault_error==77);
    assert(!policy_writes && s_twt_policy.pending && s_scan_parameters.pending);
    assert(s_config_restart.snapshot->twt_policy.post_wakeup_event);
    assert(!s_config_restart.snapshot->twt_policy.twt_enable_keep_alive);
    unsigned starts=native_starts,stops=native_stops,reads=calls;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(native_starts==starts && native_stops==stops && calls==reads && !policy_writes);
    /* Only the proven physical rebuild clears the fault. Final replay applies
     * frozen policy before the caller can publish its restored leases. */
    new_driver();assert(!s_radio.fault_stage);
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && policy_writes==1);
    assert(native_twt_policy.post_wakeup_event && !native_twt_policy.twt_enable_keep_alive && !s_twt_policy.pending);
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();assert(!allocation && !locks && !critical);return 0;
}
