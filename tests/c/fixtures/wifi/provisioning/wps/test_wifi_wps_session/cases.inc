
static void run_worker(void) {
    assert(scheduled);void (*fn)(void *)=scheduled;void *arg=scheduled_arg;scheduled=NULL;scheduled_arg=NULL;fn(arg);
}
static void tick(void) {now+=200000;(void)esp32_mquickjs_wifi_wps_service();if(scheduled)run_worker();}
static void reset(void) {
    assert(!s_wps_active && !s_wps_workers && !s_wps_handles && !helper_identity && !scheduled && !radio_active);
    alloc_fail=queue_fail=partial_start=capture_ready=hold_close=hold_helper=false;
    start_error=poll_error=finish_error=reserve_error=0;helper_ready=false;during_transfer=NULL;
    starts=finishes=prepares=closes=pin_copies=pin_commits=copies=commits=helper_drains=helper_releases=0;
    assert(!esp32_mquickjs_wifi_wps_open_runtime());
}
static esp32_mquickjs_wifi_wps_session_t *create(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};esp32_mquickjs_wifi_wps_session_t *s=NULL;
    assert(!esp32_mquickjs_wifi_wps_session_create(&config,false,1000,&s));return s;
}
static void finish_close(esp32_mquickjs_wifi_wps_session_t *s) {
    esp32_mquickjs_wifi_wps_session_close(s,false);
    for(unsigned i=0;i<8 && !s->status.retired;i++)tick();
    assert(s->status.retired && !helper_identity && !radio_active && !s_wps_active);
    esp32_mquickjs_wifi_wps_session_release(s);assert(allocs==frees);
}
static void close_during_transfer(void) {esp32_mquickjs_wifi_wps_session_close(s_wps_active,false);}
int main(void) {
    reset();esp32_mquickjs_wifi_wps_session_t *s=create();reserve_error=88;
    assert(esp32_mquickjs_wifi_wps_session_activate(s)==88 && s->status.retired && !helper_identity);
    esp32_mquickjs_wifi_wps_session_release(s);reset();
    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s) && helper_identity);
    assert(esp32_mquickjs_wifi_wps_service() && scheduled);run_worker();
    uint8_t pin[8];assert(!esp32_mquickjs_wifi_wps_session_pin(s,pin,false) && !memcmp(pin,"12345670",8));
    assert(!esp32_mquickjs_wifi_wps_session_pin(s,pin,false) && pin_copies==1 && pin_commits==1);
    assert(!esp32_mquickjs_wifi_wps_session_pin(s,NULL,true));
    assert(esp32_mquickjs_wifi_wps_session_pin(s,pin,false)==ESP_ERR_NOT_FINISHED);
    capture_ready=true;tick();assert(s->status.capture_finished && !s->status.credentials_ready);
    hold_helper=true;tick();assert(!scheduled && !copies && radio_active);
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    assert(esp32_mquickjs_wifi_wps_session_credentials(s,&credentials,false)==ESP_ERR_NOT_FINISHED);
    hold_helper=false;tick();assert(s->status.credentials_ready && copies==1 && commits==1);
    assert(!esp32_mquickjs_wifi_wps_session_credentials(s,&credentials,false) && credentials.count==1);
    assert(!esp32_mquickjs_wifi_wps_session_credentials(s,&credentials,false) && copies==1);
    esp32_mquickjs_wifi_wps_session_status_t observed;
    assert(esp32_mquickjs_wifi_wps_session_wait_begin(s,false));
    assert(!esp32_mquickjs_wifi_wps_session_observation(s,&observed));
    assert(!esp32_mquickjs_wifi_wps_session_credentials(s,NULL,true));
    esp32_mquickjs_wifi_wps_session_wait_end(s,false);
    assert(esp32_mquickjs_wifi_wps_session_observation(s,&observed));
    assert(esp32_mquickjs_wifi_wps_open_runtime()==ESP_ERR_INVALID_STATE);
    finish_close(s);reset();

    /* Closing a queued, not-yet-dispatched Session never starts WPS. */
    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));
    assert(esp32_mquickjs_wifi_wps_service() && scheduled);
    esp32_mquickjs_wifi_wps_session_close(s,false);run_worker();
    finish_close(s);assert(!starts);reset();

    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));queue_fail=true;
    assert(!esp32_mquickjs_wifi_wps_service() && !scheduled && helper_identity);
    assert(s->status.cleanup_error==ESP_ERR_NO_MEM);
    now+=2000000;(void)esp32_mquickjs_wifi_wps_service();
    assert(s->status.timed_out && s->status.error==ESP_ERR_TIMEOUT && s->status.retired && !starts);
    esp32_mquickjs_wifi_wps_session_release(s);reset();

    /* Native close and the helper fence have independent retained suffixes. */
    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));tick();hold_close=true;
    assert(!esp32_mquickjs_wifi_wps_prepare_runtime_destroy());if(scheduled)run_worker();
    assert(helper_identity && radio_active && !helper_releases);
    hold_close=false;tick();hold_helper=true;tick();assert(!closes && radio_active);
    hold_helper=false;finish_close(s);assert(esp32_mquickjs_wifi_wps_prepare_runtime_destroy());reset();

    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));tick();capture_ready=true;tick();
    during_transfer=close_during_transfer;tick();
    assert(s->status.closing && !s->status.credentials_ready);
    for(size_t i=0;i<sizeof(s->credentials);i++)assert(!((uint8_t*)&s->credentials)[i]);
    finish_close(s);reset();

    /* GC/final-owner release while a worker holds storage requests close after
     * that worker returns; only final Radio and helper retirement free it. */
    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));
    assert(esp32_mquickjs_wifi_wps_service() && scheduled);esp32_mquickjs_wifi_wps_session_release(s);run_worker();
    for(unsigned i=0;i<8 && s_wps_active;i++)tick();
    assert(!s_wps_active && !helper_identity && allocs==frees);reset();
    return 0;
}
