
#define job_api(name) esp32_mquickjs_wifi_raw_tx_periodic_job_##name
static esp32_mquickjs_wifi_raw_tx_periodic_job_status_t job_status(job_t *job) {
    esp32_mquickjs_wifi_raw_tx_periodic_job_status_t result;assert(job_api(status)(job,&result));return result;
}
static job_t *open_job(session_t *session,uint32_t count) {
    payload_t frame=packet();job_t *job=NULL;
    esp32_mquickjs_wifi_raw_tx_periodic_options_t options={.interval_us=1000,.count=count,.stop_on_error=true};
    assert(job_api(new)(session,&frame,&options,&job)==ESP_OK && !frame.data);
    assert(!job_status(job).ready && !job_status(job).retired);
    assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();return job;
}
int main(void) {
    (void)start_callback;(void)finish_callback;(void)run_job_thread;(void)enqueue;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    options_t options={.channel=6,.driver_sequence=true,.capacity=4,
        .overflow=ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH};
    session_t *session=NULL;assert(api(new)(&options,&session)==ESP_OK);tick();drain_workers();
    job_t *job=open_job(session,0);assert(job_status(job).ready && sends==1 && job->result_token.identity);
    assert(session->periodic_children==1);esp_timer_handle_t timer=job->timer;
    /* Timer queues native work without a JS poller; previous SDK packet remains
     * owned. Another deadline is skipped rather than accumulating a second. */
    fire_timer(timer);drain_workers();assert(sends==1 && job_status(job).ledger.submitted==1);
    assert(job_status(job).ledger.skipped_busy==1);
    driver_callback(&callback_info);fire_timer(timer);drain_workers();
    assert(!test_broker_status().operation_active); /* Session worker retires actual completion */
    fire_timer(timer);drain_workers();assert(sends==2 && job_status(job).ledger.completed==1);
    /* Queue saturation in the timer path retains the single native hold. */
    queue_full=true;fire_timer(timer);assert(!job_count && !job_status(job).worker_busy);queue_full=false;
    /* Parent close retains its child reservation through timer deletion, even
     * though queue completion/Radio release may have finished first. */
    api(request_close)(session);driver_callback(&callback_info);delete_error=91;
    fire_timer(timer);drain_workers();assert(!status(session).closed);
    fire_timer(timer);drain_workers();
    assert(job_status(job).ledger.completed==2 && job_status(job).cleanup_error==91);
    assert(job_status(job).timer_quiesced && !job_status(job).retired && !status(session).closed);
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t summary;
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(&summary);
    assert(summary.live==1 && summary.cleanup_pending==1 && summary.cleanup_error==91);
    assert(summary.error_generation==job_status(job).ledger.generation);
    unsigned before_stops=timer_stops;delete_error=0;fake_now+=100001;
    assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();
    assert(job_status(job).retired && timer_stops==before_stops && !session->periodic_children);
    tick();drain_workers();assert(status(session).closed);
    job_api(close)(job);job_api(release)(job);api(release)(session);assert(allocations==frees);

    /* No callback after timer retirement can refer to a later owner's storage.
     * Failed timer starts also own the stop/delete suffix before Session release. */
    session=NULL;assert(api(new)(&options,&session)==ESP_OK);tick();drain_workers();
    start_error=92;job=open_job(session,1);
    assert(job_status(job).retired && job_status(job).error==92 && !session->periodic_children);
    job_api(release)(job);start_error=0;
    payload_t frame=packet();job=NULL;
    esp32_mquickjs_wifi_raw_tx_periodic_options_t periodic={.interval_us=1000};
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_OK);
    unsigned before_create=timer_creates;job_api(stop)(job);
    assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();
    assert(job_status(job).retired && timer_creates==before_create);job_api(release)(job);job=NULL;
    /* Nth control allocation, mutex failure and invalid template preserve the
     * caller frame; failed creation never reserves a permanent child. */
    frame=packet();job=NULL;fail_control_at=control_allocations+1;
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_ERR_NO_MEM && frame.data && !job);
    fail_control_at=0;fail_mutex=true;
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_ERR_NO_MEM && frame.data && !job);
    fail_mutex=false;frame.data[1]=0x40;
    assert(job_api(new)(session,&frame,&periodic,&job)!=ESP_OK && !session->periodic_children && frame.data);
    esp32_mquickjs_memory_payload_free(frame.data);
    /* Registry and Session reservations are bounded and recover on release. */
    job_t *many[8]={0};
    for(unsigned i=0;i<8;++i) {frame=packet();assert(job_api(new)(session,&frame,&periodic,&many[i])==ESP_OK);}
    frame=packet();assert(job_api(new)(session,&frame,&periodic,&job)!=ESP_OK && frame.data && !job);
    esp32_mquickjs_memory_payload_free(frame.data);assert(session->periodic_children==8);
    for(unsigned i=0;i<8;++i)job_api(stop)(many[i]);
    assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();
    assert(!session->periodic_children);
    for(unsigned i=0;i<8;++i){assert(job_status(many[i]).retired);job_api(release)(many[i]);}
    /* Packet-copy OOM is a no-submit rejection. The template remains owned
     * until timer quiescence; stopOnError retires this failed periodic child. */
    frame=packet();periodic.stop_on_error=true;
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_OK);unsigned before_send=sends;
    fail_alloc=true;assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();fail_alloc=false;
    assert(job_status(job).retired && job_status(job).error==ESP_ERR_NO_MEM && sends==before_send);
    job_api(release)(job);job=NULL;
    /* End-to-end native proof: an uncertain periodic result cannot retire
     * before deinit. After injected physical proof it drains Session + timer. */
    send_error=89;job=open_job(session,0);timer=job->timer;
    fire_timer(timer);drain_workers();
    assert(job_status(job).ledger.uncertain && !job_status(job).retired && job->result_token.identity);
    assert(!status(session).closed && session->periodic_children==1);
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
    tick();drain_workers();fire_timer(timer);drain_workers();tick();drain_workers();
    assert(job_status(job).retired && !job_status(job).ledger.uncertain && job_status(job).ledger.aborted==1);
    assert(!job_status(job).ledger.completed && status(session).closed && !session->periodic_children);
    job_api(release)(job);job=NULL;api(release)(session);session=NULL;assert(allocations==frees);
    send_error=0;assert(api(new)(&options,&session)==ESP_OK);tick();drain_workers();
    /* Boot identities do not wrap through native/JS lifecycle reuse. */
    s_next_job_generation=UINT32_MAX;frame=packet();
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_OK && job_status(job).ledger.generation==UINT32_MAX);
    job_api(stop)(job);assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();job_api(release)(job);job=NULL;
    frame=packet();assert(job_api(new)(session,&frame,&periodic,&job)!=ESP_OK && frame.data && !session->periodic_children);
    esp32_mquickjs_memory_payload_free(frame.data);
    api(request_close)(session);tick();drain_workers();api(release)(session);assert(allocations==frees);
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(&summary);
    assert(!summary.live && !summary.retired && summary.identity_exhausted);
    assert(!critical_depth && !session_mutex_depth && !in_timer);
    return 0;
}
