
static void prepare_ap_close(void) {
    setup();ap_close_events=false;s_radio.effective_mode=native_mode=WIFI_MODE_APSTA;
    s_radio.ap_stop_phase=AP_STOP_READY;s_radio.ap_stop_error=0;
    s_radio.ap_transition_application=11;s_radio.ap_transition_station=12;s_radio.ap_transition_access_point=13;
    s_radio.leases[0]=(wifi_radio_live_lease_t){11,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION};
    s_radio.leases[1]=(wifi_radio_live_lease_t){12,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA};
    s_radio.leases[2]=(wifi_radio_live_lease_t){13,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
}
static void after_retirement(void) {
    assert(s_radio.ap_stop_phase==AP_STOP_QUIESCED && native_mode==WIFI_MODE_STA);
    /* Runtime owner retirement is a storage boundary for this isolated unit. */
    memset(s_radio.leases,0,sizeof(s_radio.leases));s_radio.ap_stop_phase=AP_STOP_IDLE;
    calls=writes=fail_at=0;
}
int main(void) {
    prepare_ap_close();
    esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.identity++;
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&stale)==ESP_ERR_INVALID_STATE && !calls);
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK);
    assert(s_inactive_history.generation==7 && s_inactive_history.known==2 && s_inactive_history.values[1]==601);
    unsigned before=calls;
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK && calls==before);
    after_retirement();wifi_radio_operation_lock();
    /* A disabled target now retains the value for deferred restoration. */
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_APSTA && !writes);
    wipe();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_APSTA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_mask==WIFI_MODE_STA);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_APSTA);
    assert(s_config_restart.snapshot->inactive_time[1]==601);
    new_driver();assert(!s_inactive_history.known && !s_inactive_history.generation);
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(inactive[0]==17 && inactive[1]==601 && !inactive_nvs_writes);
    assert(s_inactive_history.generation==8 && s_inactive_history.known==3);
    assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_APSTA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();

    prepare_ap_close();fail_at=2; /* Getter failure must not prevent AP closing. */
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK && !s_radio.fault_stage);
    assert(s_inactive_history.failed==2 && !s_inactive_history.known && s_inactive_history.error==77);
    after_retirement();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_APSTA)==77);
    assert(!allocation && !writes && !strcmp(s_radio.configuration.stage,"restart-hidden-inactive-history"));
    wifi_radio_operation_unlock();

    prepare_ap_close();assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK);
    after_retirement();wifi_radio_operation_lock();
    s_inactive_history.generation--; /* Old physical history never enters a new checkpoint. */
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_STA && !s_config_restart.snapshot->inactive_time[1]);
    wipe();wifi_radio_operation_unlock();
    assert(!locks && !critical && !helper_locks && !allocation);
    return 0;
}
