
int main(void) {
    policy_reset(true);
    esp32_mquickjs_wifi_radio_lifecycle_t token={.generation=7,.identity=41},old=token;
    s_radio.lifecycle.identity=token.identity;s_radio.lifecycle.generation=token.generation;
    wifi_radio_operation_lock();
    esp32_mquickjs_wifi_policy_write_t write;
    for(unsigned i=0;i<4;++i)assert(esp32_mquickjs_wifi_policy_apply(&s_policies,7,i,i==1,wifi_radio_policy_writer,NULL,&write)==ESP_OK);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && !s_policy_restart.owner.identity);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_APSTA)==ESP_OK);
    assert(s_policy_restart.snapshot.mask==15 && s_policy_restart.snapshot.values==2);
    old.identity++;unsigned before=calls;
    assert(wifi_radio_policy_restart_replay_locked(&old,false)==ESP_ERR_INVALID_STATE && calls==before);
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_ERR_INVALID_STATE && calls==before);
    /* Native physical deinit and subsequent init are injected boundaries here.
     * Frozen plan, ledger, phase admission and cleanup coordinator are real. */
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=8;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;s_radio.fault_stage=NULL;s_radio.fault_error=0;
    fail_at=calls+2;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==77 && s_policy_restart.completed==2);
    assert(!strcmp(s_radio.fault_stage,"restart-policy-pre-start") && s_policy_restart.owner.identity==41);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_APSTA)==ESP_OK); /* Does not recapture uncertain output. */
    fail_at=0;before=calls;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_ERR_INVALID_STATE && calls==before);
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=9;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;s_radio.fault_stage=NULL;s_radio.fault_error=0;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK && s_policy_restart.completed==14 && calls==before+3);
    before=calls;
    assert(wifi_radio_policy_restart_replay_locked(&token,true)==ESP_ERR_INVALID_STATE && calls==before);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_policy_restart_replay_locked(&token,true)==ESP_OK && s_policy_restart.completed==15 && !native_dynamic && native_11b[0] && !native_11b[1] && !native_coex);
    wifi_radio_operation_unlock();
    cleanup_error=77;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==77 && token.identity==41 && s_policy_restart.owner.identity==41);
    cleanup_error=0;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !token.identity && !s_policy_restart.owner.identity && cleanups==2);
    assert(!locks && !critical && !helper_locks);
    return 0;
}
