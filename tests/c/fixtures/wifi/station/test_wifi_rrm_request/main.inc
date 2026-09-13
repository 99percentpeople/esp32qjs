
static esp32_mquickjs_wifi_rrm_status_t state(esp32_mquickjs_wifi_rrm_request_t *r) {
    esp32_mquickjs_wifi_rrm_status_t s;assert(esp32_mquickjs_wifi_rrm_status(r,&s));return s;
}
static void progress(void) {now+=200000;assert(esp32_mquickjs_wifi_rrm_service());run_worker();esp32_mquickjs_wifi_rrm_poll_observations(false);}
static esp32_mquickjs_wifi_rrm_request_t *open_request(unsigned bytes) {
    esp32_mquickjs_wifi_rrm_request_t *r=NULL;assert(esp32_mquickjs_wifi_rrm_create(bytes,&r)==ESP_OK);
    assert(esp32_mquickjs_wifi_rrm_start(r)==ESP_OK);progress();return r;
}
int main(void) {
    reset_vendor();s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_operation_identity=1;
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&application)==ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&station)==ESP_OK);
    wifi_radio_operation_unlock();
    for(unsigned n=1;n<=2;n++) {
        alloc_calls=0;fail_alloc=n;esp32_mquickjs_wifi_rrm_request_t *r=NULL;
        assert(esp32_mquickjs_wifi_rrm_create(16,&r)==ESP_ERR_NO_MEM && !r && !live_allocs);
        assert(!s_rrm_handles && !s_rrm_reserved_bytes);
    }
    fail_alloc=0;
    esp32_mquickjs_wifi_rrm_request_t *held[8]={0},*extra=NULL;
    for(unsigned i=0;i<8;i++)assert(esp32_mquickjs_wifi_rrm_create(16,&held[i])==ESP_OK);
    assert(esp32_mquickjs_wifi_rrm_create(16,&extra)==ESP_ERR_NO_MEM);
    for(unsigned i=0;i<8;i++)esp32_mquickjs_wifi_rrm_release(held[i]);
    memset(held,0,sizeof(held));
    for(unsigned i=0;i<4;i++)assert(esp32_mquickjs_wifi_rrm_create(4096,&held[i])==ESP_OK);
    assert(esp32_mquickjs_wifi_rrm_create(1,&extra)==ESP_ERR_NO_MEM);
    for(unsigned i=0;i<4;i++)esp32_mquickjs_wifi_rrm_release(held[i]);
    assert(!s_rrm_reserved_bytes && !live_allocs && !submissions);

    esp32_mquickjs_wifi_rrm_request_t *r=open_request(16);
    assert(state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_PENDING && !state(r).retired);
    esp32_mquickjs_wifi_radio_operation_t old=state(r).operation;
    esp32_mquickjs_wifi_radio_end_operation(&old);assert(old.identity && s_radio.operation.identity==old.identity);
    wifi_radio_operation_lock();wifi_radio_release_locked(&station);wifi_radio_operation_unlock();assert(station.acquired);
    esp32_mquickjs_wifi_rrm_callback_t stale_callback=pending_callback;void *stale_identity=pending_identity;
    esp32_mquickjs_wifi_rrm_cancel(r,true);deliver(payload,16);
    assert(state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_TIMED_OUT && !state(r).retired);
    progress();assert(state(r).retired && !s_radio.operation.identity && submissions==1);
    esp32_mquickjs_wifi_rrm_release(r);assert(!live_allocs);

    r=open_request(16);assert(state(r).operation.identity!=old.identity);
    stale_callback(stale_identity,payload,16);assert(!state(r).callback_seen);
    esp32_mquickjs_wifi_rrm_sdk_result_t native;
    assert(esp32_mquickjs_wifi_radio_rrm_retire(&old,stale_callback,&native)==ESP_ERR_INVALID_STATE && !native.entered);
    deliver(payload,16);assert(state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_REPORT && !state(r).retired);
    uint8_t copy[16]={0};assert(!esp32_mquickjs_wifi_rrm_copy(r,0,copy,16));progress();
    assert(state(r).retired && esp32_mquickjs_wifi_rrm_copy(r,0,copy,16) && !memcmp(copy,payload,16));
    assert(!esp32_mquickjs_wifi_rrm_copy(r,SIZE_MAX,copy,1));
    assert(!esp32_mquickjs_wifi_rrm_copy(r,15,copy,2));
    esp32_mquickjs_wifi_rrm_close(r);assert(!esp32_mquickjs_wifi_rrm_copy(r,0,copy,1));
    esp32_mquickjs_wifi_rrm_release(r);assert(!live_allocs);

    early_report=true;r=open_request(16);assert(state(r).retired && state(r).callback_seen);
    early_report=false;esp32_mquickjs_wifi_rrm_release(r);
    r=open_request(8);deliver(payload,16);progress();assert(state(r).error==ESP_ERR_INVALID_SIZE && state(r).received_bytes==16);
    assert(!esp32_mquickjs_wifi_rrm_copy(r,0,copy,1));esp32_mquickjs_wifi_rrm_release(r);
    r=open_request(16);deliver(NULL,0);progress();assert(state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_NO_REPORT);
    esp32_mquickjs_wifi_rrm_release(r);
    r=open_request(16);assert(esp32_mquickjs_wifi_rrm_waiter_add(r));
    unsigned observed=observation_attempts;deliver(payload,16);progress();
    assert(state(r).retired && observation_attempts==observed && !esp32_mquickjs_wifi_rrm_poll_observations(false));
    esp32_mquickjs_wifi_rrm_waiter_remove(r);observation_queue_full=true;
    assert(esp32_mquickjs_wifi_rrm_poll_observations(false) && observation_attempts==observed+1);
    assert(!esp32_mquickjs_wifi_rrm_poll_observations(false));observation_queue_full=false;
    esp32_mquickjs_wifi_rrm_release(r);assert(!live_allocs);
    r=open_request(16);closing=r;close_during_copy=true;deliver(payload,16);
    assert(!close_during_copy && state(r).terminal==ESP32_MQUICKJS_WIFI_RRM_CANCELLED && !state(r).retired);
    progress();assert(state(r).retired && !state(r).retained_bytes);esp32_mquickjs_wifi_rrm_release(r);

    r=open_request(16);unsigned sent=submissions;
    dispatch_fail=true;esp32_mquickjs_wifi_rrm_close(r);progress();
    assert(!state(r).retired && state(r).cleanup_error==ESP_FAIL && s_radio.operation.identity);
    esp32_mquickjs_wifi_rrm_release(r);assert(s_rrm_handles==1);
    dispatch_fail=false;assert(!esp32_mquickjs_wifi_rrm_prepare_runtime_destroy());run_worker();
    assert(esp32_mquickjs_wifi_rrm_prepare_runtime_destroy() && !live_allocs && submissions==sent);

    r=NULL;assert(esp32_mquickjs_wifi_rrm_create(16,&r)==ESP_OK);assert(esp32_mquickjs_wifi_rrm_start(r)==ESP_OK);
    queue_full=true;assert(!esp32_mquickjs_wifi_rrm_service());assert(state(r).cleanup_error==ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_rrm_close(r);esp32_mquickjs_wifi_rrm_release(r);
    queue_full=false;progress();assert(!live_allocs && submissions==sent);

    rm_enabled=false;r=open_request(16);assert(state(r).retired && state(r).error==ESP_ERR_INVALID_STATE && submissions==sent);
    esp32_mquickjs_wifi_rrm_release(r);rm_enabled=true;
    submit_code=-12;r=open_request(16);assert(state(r).retired && state(r).submit.tx_attempted && state(r).submit.code==-12);
    esp32_mquickjs_wifi_rrm_release(r);submit_code=0;
    s_radio.next_operation_identity=UINT32_MAX;r=open_request(16);assert(state(r).operation.identity==UINT32_MAX);
    esp32_mquickjs_wifi_rrm_close(r);progress();esp32_mquickjs_wifi_rrm_release(r);sent=submissions;
    r=open_request(16);assert(state(r).retired && state(r).error==ESP_ERR_NO_MEM && submissions==sent);
    esp32_mquickjs_wifi_rrm_release(r);
    assert(!s_rrm_active && !s_rrm_handles && !s_rrm_reserved_bytes && !live_allocs && !s_radio.operation.identity);
    return 0;
}
