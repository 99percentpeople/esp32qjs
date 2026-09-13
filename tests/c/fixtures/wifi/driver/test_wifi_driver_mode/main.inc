
#define SELECT(mode) esp32_mquickjs_wifi_radio_set_mode(mode,&result)
int main(void) {
    mode_reset();assert(SELECT((wifi_mode_t)99)==ESP_ERR_INVALID_ARG && !calls);
    assert(esp32_mquickjs_wifi_radio_set_mode(WIFI_MODE_STA,NULL)==ESP_ERR_INVALID_ARG && !calls);
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(SELECT(WIFI_MODE_AP)==ESP_ERR_NOT_SUPPORTED && !calls && !result.mutation_attempted);
    assert(SELECT(WIFI_MODE_APSTA)==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    for(unsigned previous=0;previous<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 4U : 2U);++previous) {
        for(unsigned requested=0;requested<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 4U : 2U);++requested) {
            mode_reset();s_radio.effective_mode=native_mode=(wifi_mode_t)previous;
            assert(SELECT((wifi_mode_t)requested)==ESP_OK);
            assert(s_radio.effective_mode==(wifi_mode_t)requested && native_mode==(wifi_mode_t)requested);
            assert(calls==(previous==requested ? 1U : 3U) && writes==(previous==requested ? 0U : 1U));
            assert(s_radio.generation==7 && !s_radio.started && s_radio.storage==WIFI_STORAGE_RAM);
        }
    }
    for(unsigned invalid=0;invalid<14;++invalid) {
        mode_reset();
        if(invalid==0)s_radio.driver_owned=false;
        if(invalid==1)s_radio.storage_configured=false;
        if(invalid==2)s_radio.started=true;
        if(invalid==3)s_radio.stop_required=true;
        if(invalid==4)s_radio.restart_required=true;
        if(invalid==5)s_radio.lifecycle.identity=1;
        if(invalid==6)s_radio.operation.identity=1;
        if(invalid==7)s_radio.wake_locks=1;
        if(invalid==8)s_radio.promiscuous_claimed=true;
        if(invalid==9)s_tx_rate_lease.restore_pending=true;
        if(invalid==10)s_radio.fault_stage="other-fault";
        if(invalid==11)s_radio.cleanup_stage="cleanup";
        if(invalid==12)s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING;
        if(invalid==13)s_radio.storage=(wifi_storage_t)99;
        assert(SELECT(WIFI_MODE_STA)==(invalid==0 ? ESP_ERR_WIFI_NOT_INIT : ESP_ERR_INVALID_STATE));
        assert(!calls && !writes && !result.mutation_attempted);
    }
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i) {
        mode_reset();s_radio.leases[i].identity=i+1;
        assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && !calls);
    }
    for(unsigned failure=1;failure<=3;++failure) {
        mode_reset();fail_at=failure;
        assert(SELECT(WIFI_MODE_STA)==77 && result.error==77);
        assert(native_mode==WIFI_MODE_NULL && s_radio.effective_mode==WIFI_MODE_NULL);
        assert(result.mutation_attempted==(failure>1) && result.rollback_complete==(failure>1));
        assert(!s_radio.fault_stage && !s_radio.cleanup_stage && !result.persistent_mutation_possible);
    }
    for(unsigned failure=1;failure<=2;++failure) {
        mode_reset();fail_at=3;rollback_fail=failure;
        assert(SELECT(WIFI_MODE_STA)==77 && result.error==77 && result.rollback_error==88);
        assert(!result.rollback_complete && !strcmp(s_radio.cleanup_stage,"mode-rollback"));
        assert(s_radio.fault_error==77 && s_radio.cleanup_error==88);
        unsigned before=calls;fail_at=0;
        assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
    }
    mode_reset();corrupt_mode_readback=true;
    assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && result.rollback_complete && native_mode==WIFI_MODE_NULL);
    assert(!s_radio.fault_stage && s_radio.effective_mode==WIFI_MODE_NULL);
    mode_reset();corrupt_mode_readback=corrupt_mode_rollback=true;
    assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && !result.rollback_complete);
    assert(result.rollback_error==ESP_ERR_INVALID_RESPONSE && s_radio.cleanup_stage);
    mode_reset();native_mode=WIFI_MODE_STA; /* Do not overwrite an unverified external predecessor. */
    assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && !writes && !result.mutation_attempted);
    assert(!strcmp(s_radio.fault_stage,"mode-snapshot"));
    mode_reset();s_radio.effective_mode=native_mode=(wifi_mode_t)99;
    assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && !writes);
    mode_reset();s_radio.storage=native_storage=WIFI_STORAGE_FLASH;fail_at=3;
    assert(SELECT(WIFI_MODE_STA)==77 && result.rollback_complete && result.persistent_mutation_possible);
    assert(s_radio.fault_error==77 && native_mode==WIFI_MODE_NULL && !s_radio.cleanup_stage);
    assert(!locks && !critical && !helper_locks);return 0;
}
