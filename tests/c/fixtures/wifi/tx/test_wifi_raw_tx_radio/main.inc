
#define radio(name) esp32_mquickjs_wifi_radio_raw_tx_##name
#define broker(name) esp32_mquickjs_wifi_raw_tx_broker_##name
static uint8_t bytes[24]={0x80};
static uint8_t actual;
static esp32_mquickjs_wifi_raw_tx_validation_t validation;
static void *release_thread(void *unused) {
    (void)unused;release_task=true;esp32_mquickjs_wifi_radio_release(releasing_lease);return NULL;
}
static void radio_send_hook(void) {
    assert(operation_depth==1 && !critical_depth);
    if(try_release) {
        try_release=false;release_entered=false;
        assert(!pthread_create(&release_worker,NULL,release_thread,NULL));
        assert(!pthread_mutex_lock(&pause_lock));
        while(!release_entered)assert(!pthread_cond_wait(&changed,&pause_lock));
        assert(!pthread_mutex_unlock(&pause_lock));
        assert(releasing_lease->acquired && s_radio.leases[0].fixed_channel);
    }
}
static esp_err_t send_frame(esp32_mquickjs_wifi_radio_lease_t *lease,token_t *token,bool sequence) {
    sending_token=token;
    esp_err_t result=radio(submit)(lease,driver_mode==WIFI_MODE_AP ? ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT :
        ESP32_MQUICKJS_WIFI_RAW_TX_STATION,sequence,bytes,24,token,&validation,&actual);
    sending_token=NULL;return result;
}
static void acquire(esp32_mquickjs_wifi_radio_lease_t *lease) {
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX,driver_mode,lease)==ESP_OK);
    wifi_radio_operation_unlock();
}
int main(void) {
    memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);
    memcpy(destination,bytes+4,6);memcpy(source_address,bytes+10,6);memcpy(connected_ap.bssid,destination,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    esp32_mquickjs_wifi_radio_lease_t lease={0};token_t token={0};acquire(&lease);
    assert(lease.identity==123 && s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX]==1);
    esp32_mquickjs_wifi_radio_lease_t stale=lease;++stale.identity;
    assert(send_frame(&stale,&token,true)==ESP_ERR_INVALID_STATE && !sends && !registrations);
    s_radio.operation.identity=1;
    assert(send_frame(&lease,&token,true)==ESP_ERR_INVALID_STATE && !sends && !registrations);
    s_radio.operation.identity=0;
    bytes[0]=0x88;
    assert(send_frame(&lease,&token,true)==ESP_ERR_INVALID_ARG && !sends && !registrations);bytes[0]=0x80;
    channel_error=73;
    assert(send_frame(&lease,&token,true)==73 && !sends && !registrations);channel_error=0;
    /* Association getters must not gate raw frame submission. The SDK sees
     * the original sequence policy and connected-path Retry/PM/More Data bits. */
    station_connected=true;association_error=71;mac_error=72;synchronous=true;
    for(unsigned seq=0;seq<2;++seq) {
        expected_sequence=seq;
        for(unsigned flag=0x800;flag<=0x2000;flag<<=1) {
            bytes[1]=flag>>8;
            unsigned before=sends;
            assert(send_frame(&lease,&token,seq)==ESP_OK && sends==before+1);
            assert(!memcmp(driver_bytes,bytes,sizeof(bytes)));
            assert(validation==ESP32_MQUICKJS_WIFI_RAW_TX_VALID && radio(retire)(&lease,&token));
        }
    }
    expected_sequence=true;bytes[1]=0;synchronous=false;
    fail_alloc=true;
    assert(send_frame(&lease,&token,true)==ESP_ERR_NO_MEM && !token.identity);
    assert(!s_radio.leases[0].fixed_channel && !s_radio.leases[0].raw_tx_identity);fail_alloc=false;
    releasing_lease=&lease;try_release=true;
    assert(send_frame(&lease,&token,true)==ESP_OK);
    assert(!pthread_join(release_worker,NULL));
    assert(lease.acquired && actual==6 && s_radio.leases[0].raw_tx_identity==token.identity);
    assert(s_radio.leases[0].fixed_channel && s_radio.leases[0].raw_tx_channel_pinned);
    wifi_radio_operation_lock();wifi_radio_release_channel_locked(&lease);wifi_radio_operation_unlock();
    assert(s_radio.leases[0].fixed_channel);
    assert(broker(abandon)(&token));
    assert(!radio(retire)(&lease,&token));
    esp32_mquickjs_wifi_radio_release(&lease);assert(lease.acquired && allocations==frees+1);
    driver_callback(&callback_info);
    assert(!radio(retire)(&stale,&token) && token.identity);
    assert(radio(retire)(&lease,&token) && !token.identity && allocations==frees);
    assert(!s_radio.leases[0].fixed_channel && !s_radio.leases[0].raw_tx_identity);
    assert(!radio(retire)(&lease,&token));
    /* A preexisting numeric channel claim survives successful retirement. */
    s_radio.leases[0].fixed_channel=true;s_radio.leases[0].primary_channel=6;
    synchronous=true;assert(send_frame(&lease,&token,true)==ESP_OK);
    assert(radio(retire)(&lease,&token) && s_radio.leases[0].fixed_channel);
    driver_channel=11;
    unsigned calls=sends;
    assert(send_frame(&lease,&token,true)==ESP_ERR_INVALID_STATE && sends==calls && !token.identity);
    assert(s_radio.leases[0].channel_conflict);
    esp32_mquickjs_wifi_radio_release(&lease);assert(!lease.acquired && !s_radio.leases[0].identity);
    /* AP sharing admits Raw TX only on an already active required interface. */
    driver_channel=6;driver_mode=s_radio.effective_mode=WIFI_MODE_AP;
    s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP]=1;
    acquire(&lease);callback_info.ifidx=WIFI_IF_AP;
    connected_clients.num=1;memcpy(connected_clients.sta[0].mac,destination,6);
    expected_sequence=false;
    assert(send_frame(&lease,&token,false)==ESP_OK && radio(retire)(&lease,&token));
    expected_sequence=true;
    bytes[0]=0x08;bytes[1]=0x01; /* SDK, not framework, judges AP ToDS. */
    assert(send_frame(&lease,&token,true)==ESP_OK && radio(retire)(&lease,&token));
    bytes[1]=0x02;
    assert(send_frame(&lease,&token,true)==ESP_OK && radio(retire)(&lease,&token));
    /* A native rejection is preserved with its ownership; preflight must not
     * turn it into INVALID_ARG or release a possibly referenced buffer. */
    synchronous=false;send_error=81;expected_sequence=false;calls=sends;
    assert(send_frame(&lease,&token,false)==81 && sends==calls+1 && token.identity);
    assert(validation==ESP32_MQUICKJS_WIFI_RAW_TX_VALID);
    esp32_mquickjs_wifi_raw_tx_broker_status_t rejected;broker(status)(&rejected);
    assert(rejected.submit_error==81 && rejected.quarantined && !rejected.driver_accepted);
    assert(!radio(retire)(&lease,&token) && s_radio.leases[0].fixed_channel);
    assert(broker(quiesce)(7,&token)==ESP_OK && broker(reset_after_deinit)(7));
    assert(radio(retire)(&lease,&token));
    esp32_mquickjs_wifi_radio_release(&lease);assert(!lease.acquired);
    assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP]==1);
    assert(!s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX] && allocations==frees);
    /* Exercise the shared callback task boundary as well as synchronous sends. */
    assert(broker(register)(7)==ESP_OK);
    start_callback();finish_callback();
    assert(broker(unregister)(7)==ESP_OK && broker(reset_after_deinit)(7));
    assert(!operation_depth && !critical_depth);
    return 0;
}
