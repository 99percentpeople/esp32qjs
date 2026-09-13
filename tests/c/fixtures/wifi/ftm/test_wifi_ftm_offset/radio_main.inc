
static void offset_reset(void) {
    policy_reset(true);s_radio.effective_mode=WIFI_MODE_APSTA;
    memset(&s_ftm_offset,0,sizeof(s_ftm_offset));memset(&s_policy_restart,0,sizeof(s_policy_restart));
    offset_error=mode_error=0;offset_calls=mode_calls=0;mode_mismatch=false;
}
int main(void) {
    esp32_mquickjs_wifi_ftm_offset_state_t record;
#define WRITE(v) esp32_mquickjs_wifi_radio_write_ftm_offset(v,&record,&result)
    offset_reset();assert(WRITE(-32769)==ESP_ERR_INVALID_ARG && !mode_calls && !offset_calls);
    assert(WRITE(32768)==ESP_ERR_INVALID_ARG && !mode_calls);
    assert(WRITE(-32768)==ESP_OK && native_offset==-32768 && record.known && result.mutation_attempted);
    assert(!locks && !critical && !helper_locks);
    offset_error=77;assert(WRITE(32767)==77 && native_offset==32767 && record.uncertain && record.accepted_cm==-32768);
    offset_error=0;assert(WRITE(0)==ESP_OK && record.known && !record.uncertain);
    unsigned before=offset_calls;
    s_radio.started=true;assert(WRITE(1)==ESP_ERR_INVALID_STATE && offset_calls==before);s_radio.started=false;
    s_radio.leases[2].identity=55;assert(WRITE(1)==ESP_ERR_INVALID_STATE && offset_calls==before);s_radio.leases[2].identity=0;
    s_radio.operation.identity=9;assert(WRITE(1)==ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.wake_locks=1;assert(WRITE(1)==ESP_ERR_INVALID_STATE);s_radio.wake_locks=0;
    s_radio.promiscuous_claimed=true;assert(WRITE(1)==ESP_ERR_INVALID_STATE);s_radio.promiscuous_claimed=false;
    mode_error=71;assert(WRITE(1)==71 && !result.mutation_attempted);mode_error=0;
    mode_mismatch=true;assert(WRITE(1)==ESP_ERR_INVALID_STATE && !result.mutation_attempted);mode_mismatch=false;
    esp32_mquickjs_wifi_radio_lifecycle_t token={.identity=41,.generation=s_radio.generation};
    s_radio.lifecycle.identity=token.identity;s_radio.lifecycle.generation=token.generation;
    assert(WRITE(1)==ESP_ERR_INVALID_STATE && offset_calls==before);
    wifi_radio_operation_lock();
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && !s_policy_restart.owner.identity);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_APSTA)==ESP_OK);
    assert(s_policy_restart.ftm_offset.configured && s_policy_restart.ftm_offset.centimeters==0);
    esp32_mquickjs_wifi_ftm_offset_invalidate(&s_ftm_offset);++s_radio.generation;
    offset_error=88;assert(wifi_radio_policy_restart_replay_locked(&token,false)==88);
    assert(!s_policy_restart.ftm_offset_completed && !strcmp(s_radio.fault_stage,"restart-ftm-offset"));
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_APSTA)==ESP_OK); /* frozen input survives failure */
    s_radio.fault_stage=NULL;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;offset_error=0;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK && s_policy_restart.ftm_offset_completed);
    before=offset_calls;assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK && offset_calls==before);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_policy_restart_replay_locked(&token,true)==ESP_OK && offset_calls==before);
    assert(s_ftm_offset.known && s_ftm_offset.generation==s_radio.generation && native_offset==0);
    s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    esp32_mquickjs_wifi_ftm_offset_invalidate(&s_ftm_offset);++s_radio.generation;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK && offset_calls==before+1);
    wifi_radio_operation_unlock();assert(!locks && !critical);
    return 0;
}
