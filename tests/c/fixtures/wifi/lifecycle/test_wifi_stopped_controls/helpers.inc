
static bool unchanged(void) {
    wifi_radio_operation_lock();bool valid=wifi_radio_stop_snapshot_unchanged_locked();
    wifi_radio_operation_unlock();return valid;
}
static void stopped(void) {
    setup();memset(&s_policy_restart,0,sizeof(s_policy_restart));
    s_radio.event_identity=51;
    wifi_radio_operation_lock();wifi_radio_capture_stop_snapshot_locked();
    assert(wifi_radio_stop_locked()==ESP_OK);wifi_radio_operation_unlock();
    /* End the isolated STOP reservation, as the public stop coordinator does. */
    memset(&s_radio.lifecycle,0,sizeof(s_radio.lifecycle));memset(&token,0,sizeof(token));
    memset(&s_wifi_application,0,sizeof(s_wifi_application));
    memset(&s_wifi_state.radio_lease,0,sizeof(s_wifi_state.radio_lease));memset(&s_ap_lease,0,sizeof(s_ap_lease));
    s_radio.next_lifecycle_identity=41;calls=writes=fail_at=0;assert(unchanged());
}
static int rssi(void) {
    int32_t actual;
    return esp32_mquickjs_wifi_apply_connection_control(
        ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-75,&actual,&result);
}
static void reject(void) {
    esp32_mquickjs_wifi_radio_restart_selection_t mode={.mode=WIFI_MODE_AP};
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&mode)==ESP_ERR_INVALID_STATE);
    assert(!token.identity && !token.generation && mode.mode==WIFI_MODE_NULL && !s_radio.lifecycle.identity);
}
static void admit_without_history(void) {
    esp32_mquickjs_wifi_radio_restart_selection_t mode={0};
    assert(!unchanged());
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&mode)==ESP_OK);
    assert(mode.mode==WIFI_MODE_STA && token.identity==41 && !s_radio.started);
}
static void reconstruct(wifi_storage_t storage) {
    uint32_t expected_mask=native_event_mask;
    esp32_mquickjs_wifi_radio_restart_selection_t mode={0};
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&mode)==ESP_OK && mode.mode==WIFI_MODE_STA);
    wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,mode.mode)==ESP_OK);
    assert(s_config_restart.snapshot->storage==storage && s_config_restart.snapshot->tx_power==52);
    assert(s_config_restart.snapshot->event_mask==expected_mask);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,mode.mode)==ESP_OK);
    /* Physical START/event delivery remains the fixture boundary. */
    s_radio.started=s_radio.stop_required=native_running=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_commit_locked(&token,mode.mode)==ESP_OK);
    assert(s_radio.storage==storage && native_storage==storage && native_tx_power==52 && native_channel==1);
    assert(native_event_mask==expected_mask);
    wipe();wifi_radio_operation_unlock();
}
