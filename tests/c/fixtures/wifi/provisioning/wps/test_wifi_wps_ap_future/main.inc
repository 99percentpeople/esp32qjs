
static esp32_mquickjs_future_driver_state_t *captured(esp32_mquickjs_wifi_wps_ap_session_t *session,wps_operation_t op){
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),1);assert(state);
    assert(esp32_mquickjs_wifi_wps_ap_session_retain(session));
    assert(esp32_mquickjs_wifi_wps_ap_session_wait_begin(session,op==WPS_CLOSE));
    state->observation_registered=true;state->session=session;state->operation=op;state->timeout_ms=10;return state;
}
static void activate(esp32_mquickjs_wifi_wps_ap_session_t *s){assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));tick();}
static void consume_pin(esp32_mquickjs_wifi_wps_ap_session_t *s){assert(!esp32_mquickjs_wifi_wps_ap_session_pin(s,NULL,true));}
int main(void){
    wps_destroy(NULL);
    reset();esp32_mquickjs_wifi_wps_ap_session_t *s=create();activate(s);consume_pin(s);
    esp32_mquickjs_future_driver_state_t *wait=captured(s,WPS_RECEIVE);
    assert(wps_timeout(wait)==0 && wps_start(NULL,NULL,0,wait));
    now+=9999;assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    now++;assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && wait->wait_timed_out);
    assert(!s->status.closing && !s->status.timed_out);wps_destroy(wait);
    wait=captured(s,WPS_RECEIVE);assert(wps_start(NULL,NULL,0,wait));
    assert(wps_cancel(wait)==ESP32_MQUICKJS_CANCELLED && !s->status.closing);wps_destroy(wait);
    wait=captured(s,WPS_CLOSE);assert(wps_timeout(wait)==10);
    assert(wps_cancel(wait)==ESP32_MQUICKJS_CANCELLED);wps_destroy(wait);assert(!s->status.closing);
    capture_ready=true;tick();tick();wait=captured(s,WPS_RECEIVE);assert(wps_start(NULL,NULL,0,wait));
    assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && !wait->wait_timed_out && s->status.result_ready);
    esp32_mquickjs_wifi_wps_ap_session_status_t observation;
    assert(!esp32_mquickjs_wifi_wps_ap_session_observation(s,&observation));wps_destroy(wait);
    assert(esp32_mquickjs_wifi_wps_ap_session_observation(s,&observation));finish(s);

    reset();s=create();activate(s);hold_close=true;
    wait=captured(s,WPS_CLOSE);assert(wps_start(NULL,NULL,0,wait) && s->status.closing);run_worker();
    assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(wps_cancel(wait)==ESP32_MQUICKJS_CANCELLED);wps_destroy(wait);
    esp32_mquickjs_wifi_wps_ap_session_release(s);assert(s_wps_active && s_wps_handles==1);
    hold_close=false;for(unsigned i=0;i<8 && s_wps_active;i++)tick();assert(!s_wps_active && !s_wps_handles);

    reset();s=create();activate(s);consume_pin(s);wait=captured(s,WPS_RECEIVE);assert(wps_start(NULL,NULL,0,wait));
    esp32_mquickjs_wifi_wps_ap_session_release(s); /* GC releases public handle, not Future/native storage. */
    assert(!s->status.closing);wps_destroy(wait);assert(s->status.closing);
    for(unsigned i=0;i<8 && s_wps_active;i++)tick();assert(!s_wps_handles);

    reset();s=create();activate(s);consume_pin(s);wait=captured(s,WPS_RECEIVE);assert(wps_start(NULL,NULL,0,wait));
    s->deadline_us=now;assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && s->status.timed_out && !wait->wait_timed_out);
    wps_destroy(wait);if(scheduled)run_worker();finish(s);
    assert(!s_wps_handles && !s_wps_workers && allocs==frees);return 0;
}
