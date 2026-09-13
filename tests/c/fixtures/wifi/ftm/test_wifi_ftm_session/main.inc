
static esp32_mquickjs_wifi_ftm_session_t *hook_session;
static void close_and_release_during_sdk(void) {
    /* Emulate another task: only its own Session lock is taken. The SDK caller
     * remains busy and owns a worker reference for the whole report write. */
    unsigned saved=locks;locks=0;
    esp32_mquickjs_wifi_ftm_close(hook_session);
    assert(hook_session->entries && hook_session->busy);
    esp32_mquickjs_wifi_ftm_release(hook_session);hook_session=NULL;locks=saved;
}
static void work(void) {
    assert(worker_fn);void (*fn)(void *)=worker_fn;void *arg=worker_arg;worker_fn=NULL;worker_arg=NULL;fn(arg);
}
static void service(void){now_us+=100001;assert(esp32_mquickjs_wifi_ftm_service());work();}
static void drain(void) {
    terminal();service();assert(timer_pending && owners()==1);timer_fire();service();
    assert(ftm_queued && owners()==1);marker_fire();service();assert(!owners() && !s_ftm_active);
}
static void empty(void) {
    esp32_mquickjs_wifi_ftm_counts_t counts;esp32_mquickjs_wifi_ftm_counts(&counts);
    assert(!counts.handles && !counts.reserved_entries && !counts.active && !counts.worker_busy && !counts.cleanup_pending);
    assert(!allocated && !worker_fn && !locks && !critical && !ftm_critical);
}
int main(void) {
    reset_ftm();wifi_ftm_initiator_cfg_t cfg={.resp_mac={2,3,4,5,6,7},.channel=6,.frm_count=64,.burst_period=2};
    esp32_mquickjs_wifi_ftm_session_t *session=NULL,*other=NULL,*handles[8]={0};
    esp32_mquickjs_wifi_ftm_status_t status;esp32_mquickjs_wifi_ftm_counts_t counts;
    wifi_ftm_report_entry_t entry={.t1=123};
    for(unsigned n=1;n<=2;n++) {
        allocation_calls=0;fail_allocation=n;
        assert(esp32_mquickjs_wifi_ftm_create(&cfg,64,&session)==ESP_ERR_NO_MEM && !session);empty();
    }
    fail_allocation=0;
    assert(esp32_mquickjs_wifi_ftm_create(&cfg,65,&session)==ESP_ERR_INVALID_ARG && !ftm_start_calls);
    for(unsigned i=0;i<4;i++)assert(!esp32_mquickjs_wifi_ftm_create(&cfg,64,&handles[i]));
    assert(esp32_mquickjs_wifi_ftm_create(&cfg,1,&other)==ESP_ERR_NO_MEM && !other);
    for(unsigned i=4;i<8;i++)assert(!esp32_mquickjs_wifi_ftm_create(&cfg,0,&handles[i]));
    assert(esp32_mquickjs_wifi_ftm_create(&cfg,0,&other)==ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_ftm_counts(&counts);assert(counts.handles==8 && counts.reserved_entries==256);
    esp32_mquickjs_wifi_ftm_close(handles[0]);esp32_mquickjs_wifi_ftm_counts(&counts);
    assert(counts.handles==8 && counts.reserved_entries==192);
    for(unsigned i=0;i<8;i++){esp32_mquickjs_wifi_ftm_close(handles[i]);esp32_mquickjs_wifi_ftm_release(handles[i]);handles[i]=NULL;}
    empty();assert(!ftm_start_calls && !report_calls);

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));
    assert(!esp32_mquickjs_wifi_ftm_start(session));worker_full=true;
    assert(!esp32_mquickjs_wifi_ftm_service() && session->references==2 && !session->busy && !ftm_start_calls);
    esp32_mquickjs_wifi_ftm_close(session);esp32_mquickjs_wifi_ftm_release(session);session=NULL;
    assert(allocated==1 && !s_ftm_reserved_entries && s_ftm_active);
    worker_full=false;service();empty();assert(!ftm_start_calls);

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));
    assert(!esp32_mquickjs_wifi_ftm_start(session));
    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,1,&other));
    assert(esp32_mquickjs_wifi_ftm_start(other)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_ftm_close(other);esp32_mquickjs_wifi_ftm_release(other);other=NULL;
    assert(!esp32_mquickjs_wifi_ftm_report_entry(session,0,&entry) && entry.t1==123);
    service();assert(owners()==1 && ftm_start_calls==1);
    drain();assert(esp32_mquickjs_wifi_ftm_status(session,&status) && status.retired && status.report_ready && !status.close_requested);
    assert(status.native.copied_entries==4 && status.native.report.ftm_report_num_entries==70);
    assert(esp32_mquickjs_wifi_ftm_report_entry(session,3,&entry) && entry.t1==UINT64_MAX-3);
    assert(!esp32_mquickjs_wifi_ftm_report_entry(session,4,&entry));
    unsigned reads=report_calls;
    assert(esp32_mquickjs_wifi_ftm_report_entry(session,0,&entry) && report_calls==reads);
    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,0,&other));assert(!esp32_mquickjs_wifi_ftm_start(other));service();
    assert(esp32_mquickjs_wifi_ftm_report_entry(session,3,&entry) && entry.t1==UINT64_MAX-3);
    esp32_mquickjs_wifi_ftm_end(other);service();unsigned ends=ftm_end_calls;service();assert(ftm_end_calls==ends);
    drain();assert(esp32_mquickjs_wifi_ftm_status(other,&status) && status.report_ready && !status.close_requested && !status.retained_entries);
    esp32_mquickjs_wifi_ftm_close(session);assert(!esp32_mquickjs_wifi_ftm_report_entry(session,0,&entry));
    esp32_mquickjs_wifi_ftm_counts(&counts);assert(counts.handles==2 && !counts.reserved_entries);
    esp32_mquickjs_wifi_ftm_release(session);session=NULL;esp32_mquickjs_wifi_ftm_release(other);other=NULL;empty();

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));hook_session=session;
    ftm_submit_hook=close_and_release_during_sdk;assert(!esp32_mquickjs_wifi_ftm_start(session));service();
    ftm_submit_hook=NULL;session=NULL;
    assert(s_ftm_active && owners()==1 && allocated==1 && !s_ftm_reserved_entries);
    drain();empty();

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));hook_session=session;
    assert(!esp32_mquickjs_wifi_ftm_start(session));service();terminal();service();timer_fire();
    ftm_report_hook=close_and_release_during_sdk;service();ftm_report_hook=NULL;session=NULL;
    assert(allocated==1 && !s_ftm_reserved_entries && owners()==1 && ftm_queued);
    reads=report_calls;marker_fire();service();assert(report_calls==reads);empty();

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,2,&session));assert(!esp32_mquickjs_wifi_ftm_start(session));service();
    assert(!esp32_mquickjs_wifi_ftm_prepare_runtime_destroy());work();
    esp32_mquickjs_wifi_ftm_release(session);session=NULL;
    assert(s_ftm_active && owners()==1 && allocated==1 && !s_ftm_reserved_entries);
    drain();assert(esp32_mquickjs_wifi_ftm_prepare_runtime_destroy());empty();

    /* Report-drain queue failure retries the suffix without taking the report
     * again or dropping its stored native entries. */
    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,2,&session));assert(!esp32_mquickjs_wifi_ftm_start(session));service();
    terminal();service();timer_fire();ftm_post_error=93;service();reads=report_calls;
    assert(esp32_mquickjs_wifi_ftm_status(session,&status) && status.cleanup_error==93 && !status.retired && !status.report_ready);
    ftm_post_error=0;service();assert(ftm_queued && report_calls==reads);marker_fire();service();
    assert(esp32_mquickjs_wifi_ftm_status(session,&status) && status.report_ready && !status.cleanup_error);
    esp32_mquickjs_wifi_ftm_release(session);session=NULL;empty();return 0;
}
