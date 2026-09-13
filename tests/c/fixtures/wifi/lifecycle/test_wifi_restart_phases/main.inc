
static void begin_case(void) {
    setup();memset(&s_policies,0,sizeof(s_policies));memset(&s_policy_restart,0,sizeof(s_policy_restart));
}
static void end_case(void) {
    wifi_radio_operation_lock();wipe();wifi_radio_operation_unlock();
    assert(!locks && !critical && !helper_locks);
}
int main(void) {
    begin_case();
    esp32_mquickjs_wifi_radio_lifecycle_t old=token;old.identity++;
    unsigned before=calls;
    s_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){.stage="old-transaction",.error=77,.mutation_attempted=true};
    assert(esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(NULL,WIFI_MODE_STA)==ESP_ERR_INVALID_ARG);
    assert(!strcmp(s_radio.configuration.stage,"restart-checkpoint-admission") &&
        s_radio.configuration.error==ESP_ERR_INVALID_ARG && !s_radio.configuration.mutation_attempted && calls==before);
    assert(esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&token,WIFI_MODE_NULL)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&old,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_replay_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(calls==before && !allocation && !s_policy_restart.owner.identity);
    allocation_fail=true;
    assert(esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_NO_MEM);
    assert(!allocation && native_stops==0 && s_radio.started);
    end_case();

    begin_case();cleanup_error=77;
    assert(esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&token,WIFI_MODE_STA)==77);
    assert(s_config_restart.captured && allocation && s_radio.started && native_stops==1);
    void *frozen=allocation;before=calls;
    assert(esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(calls==before && allocation==frozen); /* STOP must precede retirement/rebuild. */
    cleanup_error=0;
    assert(esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_OK);
    assert(allocation==frozen && allocations==1 && native_stops==2 && !s_radio.started && !s_radio.stop_required);
    before=calls;
    assert(esp32_mquickjs_wifi_radio_replay_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(&old,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(&token,WIFI_MODE_APSTA)==ESP_ERR_INVALID_STATE);
    s_radio.wake_locks=1;
    assert(esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    s_radio.wake_locks=0;s_radio.leases[0].identity=99;
    assert(esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    s_radio.leases[0].identity=0;
    assert(calls==before && allocation==frozen && !locks);
    wifi_radio_operation_lock();new_driver();esp32_mquickjs_wifi_policy_invalidate(&s_policies);wifi_radio_operation_unlock();
    before=calls;
    assert(esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(calls==before && allocation==frozen && s_config_restart.source_generation==7);
    assert(esp32_mquickjs_wifi_radio_replay_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_OK);
    assert(allocation==frozen && s_config_restart.replay_generation==8 && !s_radio.started && !s_radio.stop_required);
    assert(s_radio.lifecycle.identity==41 && token.identity==41 && native_storage==WIFI_STORAGE_RAM);
    assert(!memcmp(native_config[0].sta.password,"secret-sta",10));
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
    end_case();

    begin_case();
    assert(esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_OK);
    wifi_radio_operation_lock();new_driver();esp32_mquickjs_wifi_policy_invalidate(&s_policies);wifi_radio_operation_unlock();
    frozen=allocation;fail_at=calls+1;
    assert(esp32_mquickjs_wifi_radio_replay_restart_lifecycle(&token,WIFI_MODE_STA)==77);
    assert(allocation==frozen && s_radio.fault_stage && token.identity==41);
    before=calls;fail_at=0;
    assert(esp32_mquickjs_wifi_radio_replay_restart_lifecycle(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(calls==before && allocation==frozen); /* Uncertain mutation is not replayed. */
    end_case();
    return 0;
}
