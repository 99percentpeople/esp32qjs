
#define SELECT(value) esp32_mquickjs_wifi_radio_set_storage(value,&result)
int main(void) {
    storage_reset();assert(SELECT((wifi_storage_t)99)==ESP_ERR_INVALID_ARG && !calls && !writes);
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_FLASH,NULL)==ESP_ERR_INVALID_ARG && !calls);
    for(unsigned invalid=0;invalid<13;++invalid) {
        storage_reset();
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
        assert(SELECT(WIFI_STORAGE_FLASH)==(invalid==0 ? ESP_ERR_WIFI_NOT_INIT : ESP_ERR_INVALID_STATE));
        assert(!calls && !writes && !result.mutation_attempted);
    }
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i) {
        storage_reset();s_radio.leases[i].identity=i+1;
        assert(SELECT(WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && !calls);
    }
    storage_reset();wifi_mode_t mode=s_radio.effective_mode;
    assert(SELECT(WIFI_STORAGE_FLASH)==ESP_OK && calls==1 && writes==1);
    assert(s_radio.storage_configured && s_radio.storage==WIFI_STORAGE_FLASH && native_storage==WIFI_STORAGE_FLASH);
    assert(SELECT(WIFI_STORAGE_FLASH)==ESP_OK && calls==2); /* Explicit acceptance, no invented getter/deduplication. */
    assert(s_radio.effective_mode==mode && s_radio.generation==7 && !s_radio.started);
    storage_reset();fail_at=1;
    assert(SELECT(WIFI_STORAGE_FLASH)==77 && calls==1 && writes==1 && result.mutation_attempted);
    assert(!s_radio.storage_configured && s_radio.storage==WIFI_STORAGE_RAM && native_storage==WIFI_STORAGE_FLASH);
    assert(s_radio.driver_owned && s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_FAULTED);
    assert(s_radio.fault_error==77 && !strcmp(s_radio.fault_stage,"storage-write"));
    assert(!result.rollback_attempted && !result.persistent_mutation_possible);
    fail_at=2;assert(SELECT(WIFI_STORAGE_RAM)==77 && calls==2 && !s_radio.storage_configured);
    s_radio.cleanup_stage="pending";assert(SELECT(WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && calls==2);
    s_radio.cleanup_stage=NULL;fail_at=0;
    assert(SELECT(WIFI_STORAGE_FLASH)==ESP_OK && calls==3 && s_radio.storage_configured);
    assert(!s_radio.fault_stage && s_radio.fault_error==ESP_OK && s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED);
    assert(!result.rollback_attempted && s_radio.generation==7 && !s_radio.started);
    assert(!locks && !critical && !helper_locks);return 0;
}
