
int main(void) {
    policy_reset(true);s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_lease_identity=1;
    esp32_mquickjs_wifi_radio_lifecycle_t token={.generation=7,.identity=41};
    s_radio.lifecycle.generation=7;s_radio.lifecycle.identity=41;
    esp32_mquickjs_wifi_radio_lease_t output={0};observed_owner=&output;
    wifi_radio_operation_lock();esp32_mquickjs_wifi_policy_write_t write;
    assert(esp32_mquickjs_wifi_policy_apply(&s_policies,7,0,true,wifi_radio_policy_writer,NULL,&write)==ESP_OK);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_STA)==ESP_OK);
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=8;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK);
    config_pre_start_error=64;
    unsigned identity=s_radio.next_lease_identity;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==64);
    assert(!acquired && !released && !s_radio.started && !output.acquired && token.identity==41);
    assert(s_radio.next_lease_identity==identity && s_policy_restart.owner.identity==41);
    config_pre_start_error=0;
    fail_at=calls+1;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==77);
    assert(!output.acquired && !output.identity && token.identity==41 && s_policy_restart.owner.identity==41);
    assert(acquired==1 && released==1 && !s_radio.leases[0].identity && s_radio.started);
    /* Inject successful physical cleanup/init into a new generation. */
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=9;
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.fault_stage=NULL;s_radio.fault_error=0;fail_at=0;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK);
    config_verify_error=66;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==66);
    assert(!output.acquired && token.identity==41 && s_policy_restart.owner.identity==41);
    assert(acquired==2 && released==2); /* Final config readback/storage commit also precede publication. */
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=10;
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    config_verify_error=0;config_power_error=65;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK);
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==65);
    assert(!output.acquired && token.identity==41 && s_policy_restart.owner.identity==41);
    assert(acquired==3 && released==3); /* Power restore failure never publishes. */
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=11;
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    config_power_error=0;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK);
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==ESP_OK);
    assert(output.acquired && output.generation==11 && !token.identity && !s_policy_restart.owner.identity);
    assert(s_policies.records[0].known && s_policies.records[0].generation==11 && native_dynamic);
    wifi_radio_release_locked(&output);
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){.generation=11,.identity=42};
    s_radio.lifecycle.identity=42;s_radio.lifecycle.generation=11;
    /* Actual resume validates the off checkpoint before any START. */
    unsigned prior_acquired=acquired;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,true,NULL)==ESP_ERR_INVALID_STATE);
    assert(acquired==prior_acquired && !s_radio.started && !output.acquired && token.identity==42);
    resume_config_snapshot_t off_snapshot={true};
    s_config_restart.snapshot=&off_snapshot;s_config_restart.captured=true;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,false,NULL)==ESP_ERR_INVALID_STATE);
    assert(acquired==prior_acquired && !s_radio.started && token.identity==42);
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&output,NULL,NULL,NULL,true,NULL)==ESP_OK);
    assert(output.acquired && token.identity==42 && s_radio.lifecycle.identity==42 && s_config_restart.snapshot);
    wifi_radio_release_locked(&output);s_config_restart.snapshot=NULL;s_config_restart.captured=false;
    wifi_radio_operation_unlock();assert(!locks && !critical && !helper_locks);
    return 0;
}
