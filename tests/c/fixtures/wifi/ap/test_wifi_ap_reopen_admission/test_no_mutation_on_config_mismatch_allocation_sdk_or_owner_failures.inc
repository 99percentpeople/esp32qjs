
int main(void) {
    for(int scenario=0;scenario<10;scenario++) {
        memset(&s_radio,0,sizeof(s_radio));allocs=queries=0;
        s_radio.generation=9;s_radio.next_lease_identity=3;s_radio.next_lifecycle_identity=41;
        s_radio.driver_owned=s_radio.storage_configured=s_radio.started=true;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        s_radio.effective_mode=s_radio.event_live=WIFI_MODE_STA;
        s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION]=1;
        s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA]=1;
        esp32_mquickjs_wifi_radio_lease_t app={9,1,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,true};
        esp32_mquickjs_wifi_radio_lease_t sta={9,2,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,true},ap={0};
        s_radio.leases[0]=(wifi_radio_live_lease_t){.identity=1,.client=app.client,.required_mode=WIFI_MODE_STA};
        s_radio.leases[1]=(wifi_radio_live_lease_t){.identity=2,.client=sta.client,.required_mode=WIFI_MODE_STA};
        wifi_config_t requested={0};requested.ap.ssid_len=4;memcpy(requested.ap.ssid,"test",4);
        requested.ap.channel=1;requested.ap.max_connection=4;requested.ap.beacon_interval=100;
        requested.ap.dtim_period=1;requested.ap.csa_count=3;
        requested.ap.pairwise_cipher=WIFI_CIPHER_TYPE_CCMP;requested.ap.sae_pwe_h2e=WPA3_SAE_PWE_UNSPECIFIED;
        stored=requested;
        fail_alloc=scenario==2;getter_error=scenario==3;
        if(scenario==1)stored.ap.ssid_hidden=true;
        if(scenario==3)memset(stored.ap.password,0x61,sizeof(stored.ap.password));
        if(scenario==4)s_radio.wake_locks=1;
        if(scenario==5)sta.generation++;
        if(scenario==6)s_radio.leases[2]=(wifi_radio_live_lease_t){.identity=7,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,.required_mode=WIFI_MODE_STA};
        if(scenario==7)s_radio.next_lease_identity=0;
        if(scenario==8)s_radio.operation.identity=7;
        if(scenario==9)requested.ap.channel=255;
        esp32_mquickjs_wifi_radio_lifecycle_t token={0};
        int err=esp32_mquickjs_wifi_radio_begin_ap_reopen(&app,&sta,&requested,&ap,&token);
        assert(!live_alloc && !depth && !critical && app.identity==1 && sta.identity==2);
        if(!scenario) {
            assert(err==ESP_OK && token.identity==41 && ap.identity==3 && ap.acquired);
            assert(s_radio.ap_reopen_pending && !s_radio.ap_reopen_attempted && s_radio.ap_transition_access_point==3);
            assert(s_radio.effective_mode==WIFI_MODE_STA && s_radio.next_lease_identity==4);
        } else {
            assert(err!=ESP_OK && !token.identity && !s_radio.lifecycle.identity && !ap.acquired);
            assert(!s_radio.ap_reopen_pending && !s_radio.fault_stage && !s_radio.cleanup_stage);
            assert(s_radio.clients[app.client]==1 && s_radio.clients[sta.client]==1);
            if(scenario>=4)assert(!allocs && !queries);
        }
    }
}
