
int main(void) {
    esp32_mquickjs_wifi_btm_query_t q={.reason=5,.count=1};
    q.candidates[0]=(esp32_mquickjs_wifi_btm_candidate_t){.bssid={2,0,0,0,0,1},.information=UINT32_MAX,
        .operating_class=81,.channel=6,.phy_type=7,.preference_set=true,.preference=0};
    char text[ESP32_MQUICKJS_WIFI_BTM_TEXT_BYTES];
    assert(esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)));
    assert(!strcmp(text," neighbor=02:00:00:00:00:01,-1,81,6,7,030100"));
    q.candidates[0].information=0x80000000U;
    assert(esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)) && strstr(text,",-2147483648,"));
    q.candidates[0].information=UINT32_MAX;
    for(unsigned i=1;i<16;i++){q.candidates[i]=q.candidates[0];q.candidates[i].bssid[5]=i+1;}
    q.count=16;assert(esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)));
    size_t length=strlen(text);assert(length<sizeof(text));
    assert(!esp32_mquickjs_wifi_btm_encode(&q,text,length) && !text[0]);
    q.count=17;assert(!esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)));
    q.count=2;q.candidates[1]=q.candidates[0];assert(!esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)));
    q.count=1;
    reset_vendor();s_radio.effective_mode=WIFI_MODE_STA;
    esp32_mquickjs_wifi_radio_lease_t app={0},sta={0},ap={0},foreign={0};
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&app)==ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&sta)==ESP_OK);
    wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_roaming_result_t r;
#define EXEC(query) esp32_mquickjs_wifi_radio_roaming(&app,&sta,&ap,query,&r)
    assert(EXEC(NULL)==ESP_OK && r.rrm && r.btm && !r.submitted);
    assert(EXEC(&q)==ESP_OK && r.entered && r.submitted && r.radio_generation==7 && submissions==1);
    query_code=-1;assert(EXEC(&q)==ESP_FAIL && r.sdk_code==-1 && r.submitted);query_code=0;
    dispatch_failure=1;assert(EXEC(&q)==ESP_FAIL && !r.entered && !r.submitted);dispatch_failure=0;
    link_error=88;assert(EXEC(&q)==88 && !r.submitted && !strcmp(r.stage,"station-link"));link_error=0;
    btm_enabled=false;assert(EXEC(&q)==ESP_ERR_INVALID_STATE && !r.submitted);btm_enabled=true;
    ap_supported=false;assert(EXEC(NULL)==ESP_OK && !r.btm);assert(EXEC(&q)==ESP_ERR_NOT_SUPPORTED && !r.submitted);ap_supported=true;
    unsigned before=dispatches;
    s_radio.operation.identity=1;assert(EXEC(&q)==ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(EXEC(&q)==ESP_ERR_INVALID_STATE);s_radio.lifecycle.identity=0;
    s_radio.cleanup_stage="injected";assert(EXEC(NULL)==ESP_ERR_INVALID_STATE);s_radio.cleanup_stage=NULL;
    ++sta.identity;assert(EXEC(&q)==ESP_ERR_INVALID_STATE);--sta.identity;
    s_radio.leases[0].fixed_channel=true;assert(EXEC(&q)==ESP_ERR_INVALID_STATE);s_radio.leases[0].fixed_channel=false;
    assert(dispatches==before);
    s_radio.effective_mode=WIFI_MODE_APSTA;
    wifi_radio_operation_lock();assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==ESP_OK);wifi_radio_operation_unlock();
    assert(EXEC(&q)==ESP_ERR_INVALID_STATE && !strcmp(r.stage,"ap-channel-permission"));
    q.allow_ap_channel_change=true;assert(EXEC(&q)==ESP_OK);
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_ERR_INVALID_STATE && !foreign.identity);
    wifi_radio_release_locked(&ap);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==ESP_OK);
    wifi_radio_operation_unlock();
    assert(EXEC(&q)==ESP_ERR_INVALID_STATE);
    assert(EXEC(NULL)==ESP_OK); /* Read query can coexist with unrelated owners. */
    s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    before=dispatches;assert(EXEC(NULL)==ESP_OK && !r.rrm && !r.btm && dispatches==before);
    assert(!locks && !critical && !in_sdk);
}
