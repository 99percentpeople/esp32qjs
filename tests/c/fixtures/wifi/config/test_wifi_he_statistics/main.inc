
static esp32_mquickjs_wifi_he_statistics_t actual;
static void fresh(void){reset();memset(&s_he_statistics,0,sizeof(s_he_statistics));
    native_statistics=(esp32_mquickjs_wifi_he_statistics_t){0};bad_statistics_read=false;
    rx_writes=retire_calls=0;memset(tx_writes,0,sizeof(tx_writes));}
static int change(bool rx,unsigned selection,bool enabled){
    return esp32_mquickjs_wifi_apply_he_statistics(rx,selection,enabled,&actual,&result);}
int main(void){
    fresh();assert(change(true,3,false)==ESP_OK && actual.ordinary && actual.multi_user);
    assert(change(false,2,true)==ESP_OK && actual.tx_mask==4);
    s_wifi_state.status.connected=true;assert(change(false,0,true)==ESP_OK && actual.tx_mask==5);
    unsigned before=calls;s_wifi_state.radio_lease.identity++;
    assert(change(false,0,false)==ESP_ERR_INVALID_STATE && calls==before);
    fresh();s_radio.leases[3]=(wifi_radio_live_lease_t){88,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    assert(change(true,1,false)==ESP_ERR_INVALID_STATE && !calls);
    fresh();s_radio.started=false;assert(change(true,1,false)==ESP_ERR_WIFI_NOT_STARTED && !calls);
    fresh();s_wifi_state.scan_in_progress=true;assert(change(true,1,false)==ESP_ERR_INVALID_STATE && !calls);
    fresh();assert(change(true,3,false)==ESP_OK);fail_at=calls+2;
    assert(change(true,1,false)==77 && !actual.ordinary && !actual.multi_user && !s_radio.fault_stage);
    fresh();bad_statistics_read=true;assert(change(true,1,false)==ESP_ERR_INVALID_RESPONSE && !rx_writes);
    fresh();assert(change(true,3,false)==ESP_OK && change(false,3,true)==ESP_OK);
    wifi_radio_operation_lock();
    assert(wifi_radio_retire_he_statistics_locked()==ESP_OK && retire_calls==1 && !native_statistics.tx_mask);
    assert(s_he_statistics.pending && !s_he_statistics.managed && s_he_statistics.saved.tx_mask==8);
    assert(wifi_radio_retire_he_statistics_locked()==ESP_OK && retire_calls==1);
    s_radio.started=false;assert(wifi_radio_restore_he_statistics_locked()==ESP_ERR_INVALID_STATE);
    s_radio.started=true;before=rx_writes;fail_at=calls+3; /* RX and voice accepted, video fails. */
    assert(wifi_radio_restore_he_statistics_locked()==77 && rx_writes==before+1 && s_he_statistics.completed==2);
    assert(s_he_statistics.pending && s_he_statistics.managed && s_radio.fault_stage);
    before=rx_writes;fail_at=0;s_radio.fault_stage=NULL;
    assert(wifi_radio_restore_he_statistics_locked()==ESP_OK && rx_writes==before);
    assert(!s_he_statistics.pending && native_statistics.tx_mask==8);
    fail_at=calls+1;
    assert(wifi_radio_retire_he_statistics_locked()==77 && s_he_statistics.managed && s_radio.cleanup_stage);
    fail_at=0;
    assert(wifi_radio_retire_he_statistics_locked()==ESP_OK && !s_he_statistics.managed && s_he_statistics.pending);
    wifi_radio_operation_unlock();
    return 0;
}
