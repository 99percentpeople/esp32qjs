
static int32_t actual;
static int change(void) {return esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,WIFI_IF_STA,9,&actual,&result);}
static int rearm(void) {return esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-75,&actual,&result);}
int main(void) {
    reset();assert(change()==ESP_OK && actual==9 && inactive[0]==9 && inactive[1]==300);unsigned total=calls;
    assert(s_radio.leases[0].identity==11 && s_radio.leases[1].identity==12);
    assert(!result.persistent_mutation_possible);
    assert(s_inactive_history.generation==7 && s_inactive_history.known==1 && s_inactive_history.values[0]==9);
    reset();s_radio.storage=WIFI_STORAGE_FLASH;
    assert(change()==ESP_OK && result.persistent_mutation_possible);
    assert(s_radio.configuration.persistent_mutation_possible);
    for(unsigned nth=1;nth<=total;++nth) {
        reset();s_radio.storage=WIFI_STORAGE_FLASH;fail_at=nth;
        assert(change()==77 && result.error==77);
        assert(result.persistent_mutation_possible==result.mutation_attempted);
        assert(s_radio.configuration.persistent_mutation_possible==result.mutation_attempted);
        if(result.mutation_attempted)assert(result.rollback_complete);
    }
    reset();s_radio.storage=WIFI_STORAGE_FLASH;fail_at=total;rollback_fail=1;
    assert(change()==77 && result.rollback_error==88 && result.persistent_mutation_possible);
    reset();s_radio.storage=WIFI_STORAGE_FLASH;
    assert(rearm()==ESP_OK && !result.persistent_mutation_possible);
    for(unsigned nth=1;nth<=total;++nth) {
        reset();fail_at=nth;assert(change()==77 && !actual && result.error==77);
        assert(!locks && !critical && !helper_locks && inactive[0]==6 && inactive[1]==300);
        if(result.mutation_attempted) {
            assert(result.rollback_complete && !s_radio.fault_stage);
            assert(s_inactive_history.known==1 && !s_inactive_history.failed && s_inactive_history.values[0]==6);
        } else {
            assert(!writes && !result.rollback_attempted);
            assert(!s_inactive_history.known && s_inactive_history.failed==1 && s_inactive_history.error==77);
        }
    }
    reset();fail_at=total;assert(change()==77);unsigned suffix=calls-total;
    for(unsigned nth=1;nth<=suffix;++nth) {
        reset();fail_at=total;rollback_fail=nth;assert(change()==77 && result.rollback_error==88);
        assert(!result.rollback_complete && s_radio.fault_error==77 && s_radio.cleanup_error==88);
        assert(!s_inactive_history.known && s_inactive_history.failed==1 && s_inactive_history.error==88);
        unsigned before=calls;fail_at=0;assert(change()==ESP_ERR_INVALID_STATE && calls==before);
    }
    reset();corrupt_readback=true;assert(change()==ESP_ERR_INVALID_RESPONSE && result.rollback_complete && inactive[0]==6);
    reset();s_wifi_state.radio_lease.generation++;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_wifi_state.radio_lease.identity++;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_wifi_application.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_wifi_state.radio_lease.acquired=false;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_wifi_state.scan_draining=true;assert(change()==ESP_ERR_INVALID_STATE && !calls && !strcmp(result.stage,"helper-admission"));
    reset();s_wifi_state.runtime_cleanup_pending=true;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_radio.leases[3]=(wifi_radio_live_lease_t){21,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX};
    assert(change()==ESP_ERR_INVALID_STATE && !calls);
    assert(rearm()==ESP_OK && actual==-75 && threshold==-75 && calls==1 && writes==1);
    assert(rearm()==ESP_OK && calls==2); /* Same value is an explicit rearm, never deduplicated. */
    reset();fail_at=1;assert(rearm()==77 && threshold==-75 && result.mutation_attempted && !result.rollback_attempted);
    assert(calls==1 && !s_radio.fault_stage && !actual);
    reset();s_radio.wake_locks=1;assert(change()==ESP_ERR_INVALID_STATE && !calls);assert(rearm()==ESP_OK);
    reset();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    memset(s_radio.leases,0,sizeof(s_radio.leases));s_wifi_application.acquired=false;s_wifi_state.radio_lease.acquired=false;s_ap_lease.acquired=false;
    assert(change()==ESP_ERR_WIFI_NOT_STARTED && !calls);assert(rearm()==ESP_OK);
    reset();assert(esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,WIFI_IF_STA,2,&actual,&result)==ESP_ERR_INVALID_ARG && !calls);
    assert(esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-101,&actual,&result)==ESP_ERR_INVALID_ARG && !calls);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    reset();s_ap_cleanup_pending=true;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();assert(esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,WIFI_IF_AP,10,&actual,&result)==ESP_OK);
    assert(inactive[0]==6 && inactive[1]==10);
    reset();s_radio.storage=WIFI_STORAGE_FLASH;
    assert(esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,WIFI_IF_AP,10,&actual,&result)==ESP_OK);
    assert(result.persistent_mutation_possible && inactive[0]==6 && inactive[1]==10);
#endif
    return 0;
}
