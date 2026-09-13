
static esp32_mquickjs_wifi_roc_session_t *hook_session;
static void close_during_submit(void) {
    /* The injected SDK is under the Radio mutex, but a concurrent close takes
     * only the independent ROC lock; emulate that separate task boundary. */
    unsigned saved=locks;locks=0;esp32_mquickjs_wifi_roc_close(hook_session);locks=saved;
}
static void work(void) {
    assert(worker_fn);void (*fn)(void *)=worker_fn;void *arg=worker_arg;worker_fn=NULL;worker_arg=NULL;fn(arg);
}
static void service(void) {now_us+=100001;assert(esp32_mquickjs_wifi_roc_service());work();}
static void roc_done(unsigned status) {
    wifi_event_roc_done_t done={.context=(uint32_t)(uintptr_t)esp32_mquickjs_wifi_action_receive,
        .channel=6,.op_id=sdk_id,.status=status};
    wifi_radio_operation_lock();wifi_radio_action_event(WIFI_EVENT_ROC_DONE,&done);wifi_radio_operation_unlock();
}
static void drain(unsigned terminal) {
    roc_done(terminal);service();assert(queued && action_owners()==1);deliver();service();assert(!action_owners());
}
int main(void) {
    reset_action();
    wifi_roc_req_t request={.ifx=WIFI_IF_STA,.type=WIFI_ROC_REQ,.channel=6,.wait_time_ms=100,
        .rx_cb=esp32_mquickjs_wifi_action_receive};
    esp32_mquickjs_wifi_roc_session_t *sessions[8]={0},*extra=NULL;
    esp32_mquickjs_wifi_roc_status_t status;
    allocation_fail=true;assert(esp32_mquickjs_wifi_roc_create(&request,&extra)==ESP_ERR_NO_MEM && !s_roc_handles);
    allocation_fail=false;
    for(unsigned i=0;i<8;++i)assert(esp32_mquickjs_wifi_roc_create(&request,&sessions[i])==0);
    assert(s_roc_handles==8 && roc_allocated==8 && !action_calls);
    assert(esp32_mquickjs_wifi_roc_create(&request,&extra)==ESP_ERR_NO_MEM);
    for(unsigned i=0;i<8;++i){esp32_mquickjs_wifi_roc_close(sessions[i]);esp32_mquickjs_wifi_roc_release(sessions[i]);sessions[i]=NULL;}
    assert(!roc_allocated && !s_roc_handles && !action_calls);

    /* Queue full keeps an opening owner, cancellation before native submission
     * eventually frees it without any SDK mutation. */
    assert(esp32_mquickjs_wifi_roc_create(&request,&extra)==0);
    assert(esp32_mquickjs_wifi_roc_start(extra)==0);worker_full=true;
    assert(!esp32_mquickjs_wifi_roc_service() && s_roc_active==extra && !action_calls);
    esp32_mquickjs_wifi_roc_close(extra);esp32_mquickjs_wifi_roc_release(extra);extra=NULL;
    worker_full=false;service();assert(!s_roc_active && !s_roc_handles && !roc_allocated && !action_calls);

    /* Natural completion and a saturated event marker queue retain the exact
     * Radio owner. A closed retained JS-equivalent handle still uses budget. */
    assert(esp32_mquickjs_wifi_roc_create(&request,&extra)==0);
    assert(esp32_mquickjs_wifi_roc_start(extra)==0);service();
    assert(esp32_mquickjs_wifi_roc_status(extra,&status) && status.submitted && !status.retired && status.error==0);
    assert(action_calls==1 && action_owners()==1 && !cancel_calls);
    roc_done(WIFI_ROC_DONE);post_error=61;service();
    assert(esp32_mquickjs_wifi_roc_status(extra,&status) && status.cleanup_error==61 && !status.retired);
    assert(action_owners()==1 && !queued && !cancel_calls);
    post_error=0;service();deliver();service();
    assert(esp32_mquickjs_wifi_roc_status(extra,&status) && status.retired && status.native.terminal_status==WIFI_ROC_DONE);
    assert(!s_roc_active && !action_owners() && s_roc_handles==1 && roc_allocated==1);
    esp32_mquickjs_wifi_roc_close(extra);esp32_mquickjs_wifi_roc_release(extra);extra=NULL;
    assert(!s_roc_handles && !roc_allocated);

    /* Timeout/GC intent racing SDK submission is never overwritten by worker
     * publication. Failed cancel retries; accepted cancel is never reissued. */
    assert(esp32_mquickjs_wifi_roc_create(&request,&extra)==0);
    hook_session=extra;roc_submit_hook=close_during_submit;cancel_error=62;
    assert(esp32_mquickjs_wifi_roc_start(extra)==0);service();roc_submit_hook=NULL;
    assert(esp32_mquickjs_wifi_roc_status(extra,&status) && status.close_requested && status.cleanup_error==62);
    unsigned cancels=cancel_calls;cancel_error=0;service();assert(cancel_calls==cancels+1);
    service();assert(cancel_calls==cancels+1 && action_owners()==1);
    esp32_mquickjs_wifi_roc_release(extra);extra=NULL;assert(roc_allocated==1);
    drain(WIFI_ROC_FAIL);assert(!roc_allocated && !s_roc_handles);

    /* Runtime teardown must wait for native terminal/fences, even after its
     * public owner is released. */
    assert(esp32_mquickjs_wifi_roc_create(&request,&extra)==0);
    assert(esp32_mquickjs_wifi_roc_start(extra)==0);service();
    assert(!esp32_mquickjs_wifi_roc_prepare_runtime_destroy());work();
    esp32_mquickjs_wifi_roc_release(extra);extra=NULL;
    assert(s_roc_active && action_owners()==1 && roc_allocated==1);
    drain(WIFI_ROC_FAIL);assert(esp32_mquickjs_wifi_roc_prepare_runtime_destroy());
    /* Successful residency with no delivered event: idle SDK proof plus
     * marker closes naturally, while the terminal status remains unknown. */
    assert(esp32_mquickjs_wifi_roc_create(&request,&extra)==0);
    assert(esp32_mquickjs_wifi_roc_start(extra)==0);service();
    quiescent_error=0;service();assert(queued && action_owners()==1);deliver();service();
    assert(esp32_mquickjs_wifi_roc_status(extra,&status) && status.retired && status.native.sdk_quiescent);
    assert(!status.native.terminal && status.native.terminal_status==-1 && status.error==0);
    esp32_mquickjs_wifi_roc_release(extra);extra=NULL;
    assert(!roc_allocated && !s_roc_handles && !locks && !critical && !roc_critical && !worker_fn);
    /* This consumer fixture injects a physical deinit attestation. The full
     * Radio producer/owner ordering is covered in test_wifi_action_recovery. */
    quiescent_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_roc_create(&request,&extra)==0);
    assert(esp32_mquickjs_wifi_roc_start(extra)==0);service();
    wifi_radio_operation_lock();
    assert(esp32_mquickjs_wifi_action_terminated(&s_action.lane,&extra->token));
    wifi_radio_operation_unlock();
    unsigned queries=quiescent_calls,cancelled=cancel_calls,markers=posts;
    service();
    assert(esp32_mquickjs_wifi_roc_status(extra,&status) && status.retired && status.native.physical_termination);
    assert(!status.native.terminal && status.native.terminal_status==-1);
    assert(status.error==ESP_ERR_INVALID_STATE && !strcmp(status.stage,"roc-native-terminated"));
    assert(quiescent_calls==queries && cancel_calls==cancelled && posts==markers && !action_owners());
    esp32_mquickjs_wifi_roc_release(extra);extra=NULL;
    assert(!roc_allocated && !s_roc_handles && !locks && !critical && !roc_critical && !worker_fn);
    return 0;
}
