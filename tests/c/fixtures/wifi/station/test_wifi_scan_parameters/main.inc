
static wifi_scan_default_params_t wanted={.scan_time={.active={.min=4,.max=90},.passive=222},.home_chan_dwell_time=50},actual;
static void source(void){reset();memset(&s_scan_parameters,0,sizeof(s_scan_parameters));
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
    scan_partial_failure=scan_corrupt=false;scan_writes=scan_resets=0;}
static int change(const wifi_scan_default_params_t *p){return esp32_mquickjs_wifi_apply_scan_parameters(p,&actual,&result);}
int main(void){
    source();assert(change(&wanted)==ESP_OK && wifi_scan_parameters_equal(&actual,&wanted));
    assert(s_scan_parameters.known && !result.persistent_mutation_possible);
    assert(change(NULL)==ESP_OK && scan_resets==1 && actual.scan_time.active.max==120);
    wifi_scan_default_params_t zero={0};assert(change(&zero)==ESP_OK && actual.home_chan_dwell_time==30);
    unsigned before=calls;zero.scan_time.active.min=121;
    assert(change(&zero)==ESP_ERR_INVALID_ARG && calls==before);
    source();fail_at=2;assert(change(&wanted)==77 && result.rollback_complete && !result.rollback_attempted);
    assert(scan_writes==1 && !s_radio.fault_stage);
    source();fail_at=2;scan_partial_failure=true;
    assert(change(&wanted)==77 && result.rollback_attempted && result.rollback_complete && scan_writes==2);
    assert(native_scan.scan_time.active.max==120);
    source();fail_at=3;rollback_fail=1;
    assert(change(&wanted)==77 && s_radio.fault_stage && s_radio.cleanup_stage);
    before=calls;assert(change(&wanted)==ESP_ERR_INVALID_STATE && calls==before);
    source();s_wifi_state.scan_draining=true;assert(change(&wanted)==ESP_ERR_INVALID_STATE && !calls);
    source();s_wifi_state.radio_lease.identity++;assert(change(&wanted)==ESP_ERR_INVALID_STATE && !calls);
    source();s_radio.leases[3]=(wifi_radio_live_lease_t){90,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    assert(change(&wanted)==ESP_ERR_INVALID_STATE && !calls);
    source();s_wifi_state.status.connected=true;assert(change(&wanted)==ESP_OK);
    source();assert(change(&wanted)==ESP_OK);wifi_radio_operation_lock();
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();s_scan_parameters.pending=true;
    before=calls;assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_AP)==ESP_OK && calls==before);
    assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==ESP_OK && wifi_scan_parameters_equal(&native_scan,&wanted));
    before=scan_writes;assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==ESP_OK && scan_writes==before);
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();s_scan_parameters.pending=true;fail_at=calls+2;
    assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==77 && s_radio.fault_stage);
    before=calls;assert(wifi_radio_restore_scan_parameters_locked(WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
    wifi_radio_operation_unlock();
    source();const char *stage=NULL;s_radio.effective_mode=WIFI_MODE_AP;
    assert(esp32_mquickjs_wifi_radio_read_scan_parameters(&actual,&stage)==ESP_ERR_INVALID_STATE && !calls);
    return 0;
}
