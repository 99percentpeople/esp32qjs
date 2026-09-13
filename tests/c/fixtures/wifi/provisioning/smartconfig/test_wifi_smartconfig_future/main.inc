
static esp32_mquickjs_future_driver_state_t *captured(esp32_mquickjs_wifi_smartconfig_session_t *session,sc_operation_t op){
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),1);assert(state);
    assert(esp32_mquickjs_wifi_smartconfig_session_retain(session));
    state->session=session;state->operation=op;state->timeout_ms=10;return state;
}
int main(void){
    reset();esp32_mquickjs_wifi_smartconfig_session_t *s=create();activate(s);tick();
    esp32_mquickjs_future_driver_state_t *wait=captured(s,SC_RECEIVE);
    assert(sc_timeout(wait)==0 && sc_start(NULL,NULL,0,wait));
    now+=9999;assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    now++;assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && wait->wait_timed_out);
    assert(!s->status.closing && !s->status.timed_out);sc_destroy(wait);
    wait=captured(s,SC_RECEIVE);assert(sc_start(NULL,NULL,0,wait));
    assert(sc_cancel(wait)==ESP32_MQUICKJS_CANCELLED && !s->status.closing);sc_destroy(wait);
    wait=captured(s,SC_CLOSE);assert(sc_timeout(wait)==10);
    assert(sc_cancel(wait)==ESP32_MQUICKJS_CANCELLED);sc_destroy(wait);assert(!s->status.closing);
    capture_ready=true;tick();wait=captured(s,SC_RECEIVE);assert(sc_start(NULL,NULL,0,wait));
    assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && !wait->wait_timed_out && s->status.credentials_ready);
    sc_destroy(wait);stop(s);

    reset();s=create();activate(s);tick();hold_close=true;
    wait=captured(s,SC_CLOSE);assert(sc_start(NULL,NULL,0,wait) && s->status.closing);run_worker();
    assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(sc_cancel(wait)==ESP32_MQUICKJS_CANCELLED);sc_destroy(wait);
    esp32_mquickjs_wifi_smartconfig_session_release(s);assert(s_sc_active && s_sc_handles==1);
    hold_close=false;tick();assert(!s_sc_active && !s_sc_handles);

    reset();s=create();activate(s);tick();wait=captured(s,SC_RECEIVE);assert(sc_start(NULL,NULL,0,wait));
    esp32_mquickjs_wifi_smartconfig_session_release(s); /* Public finalizer; Future still owns a ref. */
    assert(!s->status.closing);sc_destroy(wait);assert(s->status.closing);tick();assert(!s_sc_handles);

    reset();s=create();activate(s);tick();wait=captured(s,SC_RECEIVE);assert(sc_start(NULL,NULL,0,wait));
    s->deadline_us=now;assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && s->status.timed_out && !wait->wait_timed_out);
    sc_destroy(wait);run_worker();assert(s->status.retired);esp32_mquickjs_wifi_smartconfig_session_release(s);
    assert(!s_sc_handles && !s_sc_workers && allocs==frees);return 0;
}
