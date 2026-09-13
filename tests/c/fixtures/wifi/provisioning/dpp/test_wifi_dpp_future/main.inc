
static esp32_mquickjs_future_driver_state_t *captured(esp32_mquickjs_wifi_dpp_session_t *session,dpp_operation_t op){
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),1);assert(state);
    assert(esp32_mquickjs_wifi_dpp_session_retain(session));
    assert(esp32_mquickjs_wifi_dpp_session_wait_begin(session,op==DPP_CLOSE));
    state->observation_registered=true;state->session=session;state->operation=op;state->timeout_ms=10;return state;
}
static void activate(esp32_mquickjs_wifi_dpp_session_t *s){(void)s;tick();uri_generated=true;radio.worker.native.uri_available=true;tick();}
static void consume_uri(esp32_mquickjs_wifi_dpp_session_t *s){assert(!esp32_mquickjs_wifi_dpp_session_uri(s,NULL,0,true));}
int main(void){
    dpp_destroy(NULL);
    reset();esp32_mquickjs_wifi_dpp_session_t *s=create();activate(s);consume_uri(s);
    esp32_mquickjs_future_driver_state_t *wait=captured(s,DPP_RECEIVE);
    assert(dpp_timeout(wait)==0 && dpp_start(NULL,NULL,0,wait));
    now+=9999;assert(dpp_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    now++;assert(dpp_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && wait->wait_timed_out);
    assert(!s->status.closing && !s->status.timed_out);dpp_destroy(wait);
    wait=captured(s,DPP_RECEIVE);assert(dpp_start(NULL,NULL,0,wait));
    assert(dpp_cancel(wait)==ESP32_MQUICKJS_CANCELLED && !s->status.closing);dpp_destroy(wait);
    wait=captured(s,DPP_CLOSE);assert(dpp_timeout(wait)==10);
    assert(dpp_cancel(wait)==ESP32_MQUICKJS_CANCELLED);dpp_destroy(wait);assert(!s->status.closing);
    terminal=true;tick();tick();wait=captured(s,DPP_RECEIVE);assert(dpp_start(NULL,NULL,0,wait));
    assert(dpp_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && !wait->wait_timed_out && s->status.configs_ready);
    esp32_mquickjs_wifi_dpp_session_status_t observation;
    assert(!esp32_mquickjs_wifi_dpp_session_observation(s,&observation));dpp_destroy(wait);
    assert(esp32_mquickjs_wifi_dpp_session_observation(s,&observation));close_all(s);

    reset();s=create();activate(s);hold_native=true;
    wait=captured(s,DPP_CLOSE);assert(dpp_start(NULL,NULL,0,wait) && s->status.closing);run_worker();
    assert(dpp_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(dpp_cancel(wait)==ESP32_MQUICKJS_CANCELLED);dpp_destroy(wait);
    esp32_mquickjs_wifi_dpp_session_release(s);assert(s_dpp_active && s_dpp_handles==1);
    hold_native=false;for(unsigned i=0;i<8 && s_dpp_active;i++)tick();assert(!s_dpp_active && !s_dpp_handles);

    reset();s=create();activate(s);consume_uri(s);wait=captured(s,DPP_RECEIVE);assert(dpp_start(NULL,NULL,0,wait));
    esp32_mquickjs_wifi_dpp_session_release(s); /* GC releases public handle, not Future/native storage. */
    assert(!s->status.closing);dpp_destroy(wait);assert(s->status.closing);
    for(unsigned i=0;i<8 && s_dpp_active;i++)tick();assert(!s_dpp_handles);

    reset();s=create();activate(s);consume_uri(s);wait=captured(s,DPP_RECEIVE);assert(dpp_start(NULL,NULL,0,wait));
    s->deadline_us=now;assert(dpp_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && s->status.timed_out && !wait->wait_timed_out);
    dpp_destroy(wait);if(scheduled)run_worker();close_all(s);
    reset();s=create();finish_capture();assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
    wait=captured(s,DPP_CONNECT);wait->configuration_index=2;
    assert(!dpp_start(NULL,NULL,0,wait)&&!wait->started);dpp_cancel(wait);dpp_destroy(wait);assert(!s->status.closing);
    wait=captured(s,DPP_CONNECT);wait->timeout_ms=1000;assert(!dpp_timeout(wait)&&dpp_start(NULL,NULL,0,wait));
    tick();tick();assert(!s->status.connected&&connections==1);
    connection_terminal=ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS;tick();tick();
    assert(dpp_poll(wait)==ESP32_MQUICKJS_FUTURE_READY&&s->status.connected);
    assert(!esp32_mquickjs_wifi_dpp_session_observation(s,&observation));dpp_destroy(wait);
    assert(esp32_mquickjs_wifi_dpp_session_observation(s,&observation));close_all(s);
    reset();s=create();finish_capture();assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
    wait=captured(s,DPP_CONNECT);wait->timeout_ms=1000;assert(dpp_start(NULL,NULL,0,wait));
    dpp_cancel(wait);assert(s->status.closing);dpp_destroy(wait);if(scheduled)run_worker();close_all(s);
    assert(!s_dpp_handles && !s_dpp_workers && allocations==frees);return 0;
}
