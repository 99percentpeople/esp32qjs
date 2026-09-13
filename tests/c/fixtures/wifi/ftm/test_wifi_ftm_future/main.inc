
static esp32_mquickjs_future_driver_state_t *captured(esp32_mquickjs_wifi_ftm_session_t *session,ftm_operation_t operation) {
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),1);assert(state);
    assert(esp32_mquickjs_wifi_ftm_retain(session));state->session=session;state->operation=operation;state->timeout_ms=10;return state;
}
int main(void) {
    reset_ftm();wifi_ftm_initiator_cfg_t cfg={.resp_mac={2,3,4,5,6,7},.channel=6,.frm_count=32};
    esp32_mquickjs_wifi_ftm_session_t *session=NULL;
    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));assert(!esp32_mquickjs_wifi_ftm_start(session));service();
    esp32_mquickjs_future_driver_state_t *receive=captured(session,FTM_RECEIVE);
    unsigned ends=ftm_end_calls;assert(ftm_timeout(receive)==0);assert(ftm_start(NULL,NULL,0,receive));
    now_us+=9999;assert(ftm_poll(receive)==ESP32_MQUICKJS_FUTURE_PENDING);
    ++now_us;assert(ftm_poll(receive)==ESP32_MQUICKJS_FUTURE_READY && receive->receive_timed_out);
    assert(!session->status.end_requested && !session->status.close_requested && ftm_end_calls==ends && owners()==1);
    ftm_destroy(receive);
    receive=captured(session,FTM_RECEIVE);assert(ftm_start(NULL,NULL,0,receive));
    assert(ftm_cancel(receive)==ESP32_MQUICKJS_CANCEL_REQUESTED && !session->status.end_requested);ftm_destroy(receive);
    esp32_mquickjs_future_driver_state_t *end=captured(session,FTM_END);
    assert(ftm_timeout(end)==10);assert(ftm_start(NULL,NULL,0,end));if(worker_fn)work();
    assert(session->status.end_requested && !session->status.close_requested && ftm_end_calls==ends+1);
    assert(ftm_cancel(end)==ESP32_MQUICKJS_CANCEL_REQUESTED);ftm_destroy(end);
    assert(!session->status.close_requested && session->entries);drain();
    assert(session->status.report_ready && !session->status.close_requested);
    end=captured(session,FTM_END);assert(ftm_start(NULL,NULL,0,end) && ftm_poll(end)==ESP32_MQUICKJS_FUTURE_READY);
    ftm_destroy(end);esp32_mquickjs_wifi_ftm_release(session);session=NULL;empty();

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));
    esp32_mquickjs_future_driver_state_t *open=heap_caps_calloc(1,sizeof(*open),1);assert(open);
    open->session=session;open->operation=FTM_OPEN;open->timeout_ms=10;session=NULL;
    assert(ftm_start(NULL,NULL,0,open) && worker_fn);
    assert(ftm_cancel(open)==ESP32_MQUICKJS_CANCEL_REQUESTED);ftm_destroy(open);work();empty();

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));assert(!esp32_mquickjs_wifi_ftm_start(session));service();
    esp32_mquickjs_future_driver_state_t *close=captured(session,FTM_CLOSE);
    assert(ftm_start(NULL,NULL,0,close));if(worker_fn)work();
    assert(session->status.close_requested && !session->entries && owners()==1);
    assert(ftm_cancel(close)==ESP32_MQUICKJS_CANCEL_REQUESTED);ftm_destroy(close);
    esp32_mquickjs_wifi_ftm_release(session);session=NULL;
 /* Cancel/destruction already queued cleanup with its own native reference. */
 assert(worker_fn);work();drain();empty();return 0;
}
