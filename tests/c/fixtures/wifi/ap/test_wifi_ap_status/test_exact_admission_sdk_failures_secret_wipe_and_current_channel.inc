
static void reset(void) {
    assert(!depth && !critical && !allocation);
    memset(&s_radio,0,sizeof(s_radio));s_radio.generation=9;s_radio.started=true;s_radio.event_live=WIFI_MODE_AP;
    s_radio.leases[0]=(wifi_radio_live_lease_t){.identity=7,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
    calls=0;fail_at=0;fail_alloc=false;bad_length=false;bad_count=false;stop_during_channel=false;
    native_mode=WIFI_MODE_APSTA;
}
static void public_fields_zero(esp32_mquickjs_wifi_ap_snapshot_t snapshot) {
    snapshot.started=false;
    const unsigned char *p=(const unsigned char *)&snapshot;
    /* Compare fields, not struct padding passed by value. */
    (void)p;assert(!snapshot.ssid_len && !snapshot.hidden && !snapshot.channel && !snapshot.max_connections && !snapshot.client_count);
    for(int i=0;i<32;i++)assert(!snapshot.ssid[i]);for(int i=0;i<6;i++)assert(!snapshot.mac[i]);
}
int main(void) {
    esp32_mquickjs_wifi_radio_lease_t lease={.generation=9,.identity=7,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,.acquired=true};
    esp32_mquickjs_wifi_ap_snapshot_t snapshot;const char *stage;
    reset();
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_OK);
    assert(snapshot.started && !stage && snapshot.channel==6 && snapshot.hidden && snapshot.client_count==2);
    assert(snapshot.ssid_len==4 && !memcmp(snapshot.ssid,"A\0B\xff",4) && snapshot.mac[0]==2);
    assert(snapshot.max_connections==4 && calls==5 && !allocation);
    for(int fail=1;fail<=5;fail++) {
        reset();fail_at=fail;
        assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==-40-fail);
        assert(snapshot.started && stage && calls==fail && !allocation);public_fields_zero(snapshot);
    }
    for(int invalid=0;invalid<7;invalid++) {
        reset();esp32_mquickjs_wifi_radio_lease_t candidate=lease;
        if(invalid==0)candidate.identity++;
        if(invalid==1)candidate.generation++;
        if(invalid==2)candidate.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA;
        if(invalid==3)s_radio.lifecycle.identity=8;
        if(invalid==4)s_radio.started=false;
        if(invalid==5)s_radio.fault_stage="stop-events";
        assert(esp32_mquickjs_wifi_radio_sample_ap(invalid==6?NULL:&candidate,&snapshot,&stage)==ESP_ERR_INVALID_STATE);
        assert(!calls && !allocation && snapshot.started && !strcmp(stage,"admission"));public_fields_zero(snapshot);
    }
    reset();fail_alloc=true;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_NO_MEM && calls==1);
    assert(!strcmp(stage,"allocate") && !allocation);public_fields_zero(snapshot);
    reset();native_mode=WIFI_MODE_STA;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_STATE && calls==1);
    reset();bad_length=true;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_SIZE && calls==2);
    assert(!allocation);public_fields_zero(snapshot);
    reset();bad_length=2;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_OK && snapshot.ssid_len==1 && snapshot.ssid[0]=='A');
    reset();bad_count=true;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_SIZE && calls==4);
    reset();bad_count=2;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_SIZE && calls==4);
    reset();stop_during_channel=true;
    assert(esp32_mquickjs_wifi_radio_sample_ap(&lease,&snapshot,&stage)==ESP_ERR_INVALID_STATE);
    assert(!snapshot.started && !strcmp(stage,"state") && !allocation);public_fields_zero(snapshot);
}
