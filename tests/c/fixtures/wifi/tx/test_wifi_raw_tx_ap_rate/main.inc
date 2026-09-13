
#define radio(n) esp32_mquickjs_wifi_radio_##n
static wifi_tx_rate_config_t requested={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_24M};
static esp32_mquickjs_wifi_radio_lifecycle_t token;
static esp32_mquickjs_wifi_radio_lease_t lease;
static wifi_config_t snapshot;
static uint8_t channel;
static wifi_mode_t selected_mode;
static void setup(void) {
    memset(&s_radio,0,sizeof(s_radio));s_radio.generation=7;s_radio.next_lease_identity=1;s_radio.next_lifecycle_identity=1;
    s_radio.driver_owned=s_radio.storage_configured=true;s_radio.effective_mode=WIFI_MODE_AP;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_tx_rates=(esp32_mquickjs_wifi_tx_rate_state_t){.next_identity=2};
    native_rate=(wifi_tx_rate_config_t){.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
    s_tx_rates.records[1]=(esp32_mquickjs_wifi_tx_rate_record_t){.known=true,.generation=7,.write_identity=1,.config=native_rate};
    memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(&token,0,sizeof(token));memset(&lease,0,sizeof(lease));
    memset(&stored,0,sizeof(stored));stored.ap.channel=6;stored.ap.ssid_len=4;memcpy(stored.ap.ssid,"test",4);
    writes=starts=stops=waits=gets=allocations=frees=wipes=0;config_error=mode_error=validation_error=0;
    next_write[0]=next_write[1]=stop_error=wait_error=start_error=0;allocation_fail=stop_observed=false;channel=99;selected_mode=WIFI_MODE_NULL;
}
static int begin(void) {
    int err=radio(begin_raw_tx_ap_rate)(&requested,&token,&selected_mode);
    if(!err)err=radio(copy_stopped_ap_configuration)(&token,&snapshot);
    return err;
}
static int start(void) {return radio(start_raw_tx_ap_rate)(&token,selected_mode,&snapshot,&requested,&lease,&channel);}
static int close_rate(void) {return radio(quiesce_raw_tx_ap_rate)(&lease,&token);}
int main(void) {
    (void)esp32_mquickjs_wifi_radio_5ghz_channel_bit;
    setup();assert(!begin() && token.identity && !writes && !starts);
    assert(!start() && !token.identity && lease.acquired && channel==6 && starts==1 && writes==1);
    assert(allocations==frees && wipes==1);
    s_radio.leases[0].raw_tx_identity=55;
    assert(close_rate()==ESP_ERR_INVALID_STATE && lease.acquired && !token.identity && !stops);
    s_radio.leases[0].raw_tx_identity=0;
    s_radio.next_lifecycle_identity=0;
    assert(close_rate()==ESP_ERR_NO_MEM && !stops && lease.acquired);s_radio.next_lifecycle_identity=2;
    wait_error=51;assert(close_rate()==51 && stops==1 && writes==1 && !token.identity && lease.acquired);
    wait_error=0;next_write[0]=52;
    assert(close_rate()==52 && stops==1 && writes==3 && !token.identity && lease.acquired);
    assert(!close_rate() && token.identity && !lease.acquired && stops==1 && native_rate.rate==WIFI_PHY_RATE_6M);
    assert(!radio(check_stopped_lifecycle)(&token,true));
    unsigned before=writes;assert(!close_rate() && writes==before && stops==1);
    esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.generation++;
    assert(radio(quiesce_raw_tx_ap_rate)(&lease,&stale)==ESP_ERR_INVALID_STATE);
    setup();assert(!begin());stored.ap.password[0]='x';
    assert(start()==ESP_ERR_INVALID_RESPONSE && token.identity && !lease.acquired && !writes && !starts);
    assert(allocations==frees && !close_rate());
    setup();assert(!begin());allocation_fail=true;
    assert(start()==ESP_ERR_NO_MEM && token.identity && !lease.acquired && !writes && !starts);
    allocation_fail=false;assert(!close_rate());
    setup();assert(!begin());start_error=53;
    assert(start()==53 && !token.identity && lease.acquired && s_tx_rate_lease.identity);
    assert(!close_rate() && token.identity && !lease.acquired && s_radio.fault_error==53);
    assert(!radio(check_stopped_lifecycle)(&token,true));
    setup();assert(!begin());next_write[0]=54;next_write[1]=0;
    assert(start()==54 && token.identity && lease.acquired && !s_tx_rate_lease.identity && !starts);
    assert(!close_rate() && !lease.acquired && token.identity);
    setup();assert(!begin());next_write[0]=54;next_write[1]=55;
    assert(start()==54 && !token.identity && lease.acquired && s_tx_rate_lease.restore_pending);
    assert(!close_rate() && token.identity && !lease.acquired);
    setup();s_radio.leases[0].identity=77;
    assert(begin()==ESP_ERR_INVALID_STATE && !token.identity && !writes && !gets);
    setup();s_radio.next_lifecycle_identity=0;
    assert(begin()==ESP_ERR_NO_MEM && !writes && !gets);
    setup();s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(!begin() && selected_mode==WIFI_MODE_APSTA && !writes && !starts);
    assert(radio(start_raw_tx_ap_rate)(&token,WIFI_MODE_AP,&snapshot,&requested,&lease,&channel)==ESP_ERR_INVALID_STATE);
    assert(!lease.acquired && !writes && token.identity);
    s_tx_rates.records[0]=(esp32_mquickjs_wifi_tx_rate_record_t){.known=true,.generation=7,.write_identity=2,
        .config={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_9M}};s_tx_rates.next_identity=3;
    esp32_mquickjs_wifi_tx_rate_record_t station_rate=s_tx_rates.records[0];
    assert(!start() && s_radio.effective_mode==WIFI_MODE_APSTA && s_radio.leases[0].required_mode==WIFI_MODE_APSTA);
    assert(!close_rate() && s_radio.effective_mode==WIFI_MODE_APSTA);
    assert(!memcmp(&station_rate,&s_tx_rates.records[0],sizeof(station_rate)));
    setup();s_radio.effective_mode=WIFI_MODE_STA;
    assert(begin()==ESP_ERR_INVALID_STATE && selected_mode==WIFI_MODE_NULL && !token.identity && !gets);
    return 0;
}
