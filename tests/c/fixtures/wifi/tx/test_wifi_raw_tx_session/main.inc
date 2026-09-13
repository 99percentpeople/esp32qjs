
#define api(name) esp32_mquickjs_wifi_raw_tx_session_##name
#define service() esp32_mquickjs_wifi_raw_tx_sessions_service()
typedef esp32_mquickjs_wifi_raw_tx_session_options_t options_t;
typedef esp32_mquickjs_wifi_raw_tx_session_status_t session_status_t;
typedef esp32_mquickjs_wifi_raw_tx_flush_token_t flush_t;
typedef esp32_mquickjs_wifi_raw_tx_flush_status_t flush_status_t;
typedef esp32_mquickjs_wifi_raw_tx_admission_t admission_t;
static void tick(void) {fake_now+=100001;(void)service();}
static payload_t packet(void) {
    uint8_t *bytes=heap_caps_malloc(24,MALLOC_CAP_8BIT);assert(bytes);memset(bytes,0,24);bytes[0]=0x80;
    memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);return (payload_t){bytes,24};
}
static admission_t enqueue(session_t *session,unsigned count) {
    payload_t frames[4]={{0}},removed[4]={{0}};admission_t admission={0};
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    assert(count<=4);for(unsigned i=0;i<count;++i)frames[i]=packet();
    assert(api(admit)(session,frames,count,removed,4,&admission,&validation)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK);
    for(unsigned i=0;i<count;++i)assert(!frames[i].data);
    for(unsigned i=0;i<4;++i)if(removed[i].data)esp32_mquickjs_memory_payload_free(removed[i].data);
    return admission;
}
static session_status_t status(session_t *session) {
    session_status_t status;assert(api(status)(session,&status));return status;
}
static void *run_job_thread(void *opaque) {(void)opaque;run_job();return NULL;}
int main(void) {
    (void)start_callback;(void)finish_callback;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    options_t options={.channel=6,.driver_sequence=true,.capacity=4,
        .overflow=ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH};
    session_t *a=NULL,*b=NULL;session_status_t sa,sb;
    for(unsigned nth=1;nth<=2;++nth) {
        control_allocations=0;fail_control_at=nth;
        assert(api(new)(&options,&a)==ESP_ERR_NO_MEM && !a && allocations==frees && !acquires);
    }
    fail_control_at=0;fail_mutex=true;
    assert(api(new)(&options,&a)==ESP_ERR_NO_MEM && !a && allocations==frees);fail_mutex=false;
    options_t invalid=options;invalid.capacity=0;
    assert(api(new)(&invalid,&a)==ESP_ERR_INVALID_ARG && !a);
    invalid=options;invalid.channel=15;assert(api(new)(&invalid,&a)==ESP_ERR_INVALID_ARG && !a);
    assert(api(new)(&options,&a)==ESP_OK && api(new)(&options,&b)==ESP_OK);
    assert(!status(a).open_complete && !acquires);
    tick();assert(job_count==2);run_job();run_job();
    assert(status(a).open_complete && status(b).open_complete && live_leases==2);
    /* All validation precedes admission; a bad second packet cannot enqueue
     * the first or consume/drop an older batch. */
    payload_t invalid_batch[2]={packet(),packet()},removed[4]={{0}};admission_t admission={.first_sequence=99};
    invalid_batch[1].data[1]=0x40;esp32_mquickjs_wifi_raw_tx_validation_t validation;
    assert(api(admit)(a,invalid_batch,2,removed,4,&admission,&validation)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID);
    assert(validation==ESP32_MQUICKJS_WIFI_RAW_TX_PROTECTED_FRAME && admission.first_sequence==99 && !status(a).queued);
    esp32_mquickjs_memory_payload_free(invalid_batch[0].data);esp32_mquickjs_memory_payload_free(invalid_batch[1].data);
    admission=enqueue(a,2);assert(admission.first_sequence==1 && admission.last_sequence==2);
    enqueue(b,1);flush_t fa={0},fb={0};flush_status_t fsa,fsb;
    assert(api(flush_begin)(a,&fa)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK);
    assert(api(flush_begin)(b,&fb)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK);
    tick();assert(job_count==2);run_job();run_job();
    sa=status(a);sb=status(b);
    assert(sends==1 && sa.active_sequence==1 && sa.queued==1 && sb.active_sequence==0 && sb.queued==1);
    tick();assert(!job_count); /* waiting/native pending Sessions do not flood worker queue */
    enqueue(a,1); /* after fa: never extends its fence */
    driver_callback(&callback_info);tick();assert(job_count==1);run_job();
    assert(api(flush_status)(a,&fa,&fsa) && fsa.pending==1 && fsa.fence==2 && fsa.totals.succeeded==1);
    tick();assert(job_count==2);run_job();run_job();
    assert(sends==2 && !status(a).active_sequence && status(b).active_sequence==1);
    api(request_close)(b);tick();assert(job_count==1);run_job();
    sb=status(b);assert(sb.close_requested && !sb.closed && sb.active_sequence==1 && sb.cleanup_error);
    api(release)(b); /* Only native cleanup and the independent flush ref remain. */
    assert(api(flush_status)(b,&fb,&fsb) && fsb.pending==1);
    driver_callback(&callback_info);tick();assert(job_count==1);run_job();
    assert(status(b).closed && live_leases==1);
    assert(api(flush_status)(b,&fb,&fsb) && !fsb.pending && fsb.totals.succeeded==1);
    flush_t stale=fb;assert(api(flush_release)(b,&fb));b=NULL;
    assert(!api(flush_release)(a,&stale)); /* cannot release another generation */
    tick();assert(job_count==1);run_job();assert(status(a).active_sequence==2 && sends==3);
    api(request_close)(a);tick();assert(job_count==1);run_job();
    assert(status(a).totals.dropped==1); /* later packet 3, outside fa */
    stop_error=87;driver_callback(&callback_info);tick();assert(job_count==1);run_job();
    sa=status(a);assert(!sa.closed && !sa.active_sequence && !live_leases && sa.cleanup_error==87);
    assert(sa.lane_identity && !strcmp(sa.cleanup_stage,"radio-release-stop"));
    assert(api(flush_status)(a,&fa,&fsa) && !fsa.pending && fsa.totals.admitted==2 && fsa.totals.succeeded==2 && !fsa.totals.dropped);
    esp32_mquickjs_wifi_raw_tx_lane_token_t other={0};
    assert(esp32_mquickjs_wifi_raw_tx_lane_request(&other)==ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK);
    assert(!esp32_mquickjs_wifi_raw_tx_lane_acquire(&other));
    unsigned before_releases=releases;stop_error=0;api(release)(a);tick();assert(job_count==1);run_job();
    assert(status(a).closed && releases==before_releases);
    assert(esp32_mquickjs_wifi_raw_tx_lane_acquire(&other) && esp32_mquickjs_wifi_raw_tx_lane_release(&other));
    assert(api(flush_release)(a,&fa));a=NULL;assert(allocations==frees);
    assert(esp32_mquickjs_wifi_raw_tx_sessions_drained());

    /* Failed partial Radio open owns its cleanup; queue saturation before open
     * retains storage without side effects and supports close-before-start. */
    acquire_error=88;assert(api(new)(&options,&a)==ESP_OK);tick();run_job();
    sa=status(a);assert(sa.faulted && sa.closed && sa.error==88 && !live_leases);
    api(release)(a);a=NULL;acquire_error=0;assert(allocations==frees);
    assert(api(new)(&options,&a)==ESP_OK);queue_full=true;unsigned before_acquires=acquires;
    tick();assert(!job_count && acquires==before_acquires && !status(a).worker_busy);
    api(request_close)(a);queue_full=false;tick();run_job();
    assert(status(a).closed && acquires==before_acquires);api(release)(a);a=NULL;assert(allocations==frees);

    /* Real worker thread paused inside the Radio submit boundary. Main thread
     * can query and close without holding the driver mutex or freeing active
     * bytes. A close that races an in-progress call cannot promise no RF. */
    assert(api(new)(&options,&a)==ESP_OK);tick();run_job();enqueue(a,2);fa=(flush_t){0};
    assert(api(flush_begin)(a,&fa)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK);
    block_submit=true;tick();assert(job_count==1);pthread_t worker;
    assert(!pthread_create(&worker,NULL,run_job_thread,NULL));
    assert(!pthread_mutex_lock(&submit_pause_lock));
    while(!submit_paused)assert(!pthread_cond_wait(&submit_changed,&submit_pause_lock));
    assert(!pthread_mutex_unlock(&submit_pause_lock));
    sa=status(a);assert(sa.worker_busy && sa.active_sequence==1 && sa.queued==1);
    unsigned owned_during_submit=allocations-frees;
    api(request_close)(a);payload_t after_close=packet();memset(removed,0,sizeof(removed));
    assert(api(admit)(a,&after_close,1,removed,4,&admission,&validation)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_CLOSED);
    assert(after_close.data);esp32_mquickjs_memory_payload_free(after_close.data);
    assert(allocations-frees==owned_during_submit);api(release)(a); /* flush + cleanup + worker retain */
    assert(!pthread_mutex_lock(&submit_pause_lock));submit_proceed=true;
    assert(!pthread_cond_broadcast(&submit_changed));assert(!pthread_mutex_unlock(&submit_pause_lock));
    assert(!pthread_join(worker,NULL));block_submit=false;
    assert(status(a).close_requested && !status(a).closed && status(a).totals.dropped==1);
    driver_callback(&callback_info);tick();run_job();
    assert(status(a).closed && api(flush_status)(a,&fa,&fsa) && !fsa.pending && fsa.totals.dropped==1);
    assert(api(flush_release)(a,&fa));a=NULL;assert(allocations==frees);

    /* Temporary-rate Sessions retain their real arbiter grant while idle and
     * during failed restoration. Radio restoration itself has its own fixture. */
    options.rate_set=true;options.rate=(wifi_tx_rate_config_t){.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_9M};
    assert(api(new)(&options,&a)==ESP_OK);tick();run_job();
    sa=status(a);assert(sa.open_complete && sa.lane_identity && rate_owner);
    tick();assert(!job_count && !status(a).worker_busy); /* idle grant is not work */
    stop_error=91;api(request_close)(a);tick();run_job();
    sa=status(a);assert(!sa.closed && sa.lane_identity && sa.cleanup_error==91 && live_leases==1);
    stop_error=0;tick();run_job();
    assert(status(a).closed && !status(a).lane_identity && !rate_owner && !live_leases);
    api(release)(a);a=NULL;options.rate_set=false;assert(allocations==frees);

    /* Closed controls with live caller refs continue to count toward the bound. */
    session_t *many[ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS]={0};
    for(unsigned i=0;i<ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS;++i) {
        assert(api(new)(&options,&many[i])==ESP_OK);api(request_close)(many[i]);
    }
    assert(api(new)(&options,&a)==ESP_ERR_INVALID_STATE && !a);tick();
    while(job_count)run_job();
    assert(api(new)(&options,&a)==ESP_ERR_INVALID_STATE && !a);
    for(unsigned i=0;i<ESP32_MQUICKJS_WIFI_RAW_TX_MAX_SESSIONS;++i)api(release)(many[i]);
    assert(allocations==frees);

    /* Broker allocation failure before SDK ownership rejects the active packet
     * and retires its Session; it is distinct from uncertain SDK submission. */
    assert(api(new)(&options,&a)==ESP_OK);tick();run_job();enqueue(a,1);
    fail_alloc=true;tick();run_job();fail_alloc=false;
    sa=status(a);assert(sa.faulted && sa.closed && sa.totals.rejected==1 && !live_leases);
    api(release)(a);a=NULL;assert(allocations==frees);

    /* SDK error after token publication: keep active payload, driver copy,
     * exact lease and grant. Runtime prepare cannot claim native termination. */
    s_next_session_generation=UINT32_MAX;
    assert(api(new)(&options,&a)==ESP_OK);tick();run_job();enqueue(a,1);
    assert(status(a).generation==UINT32_MAX);
    assert(api(new)(&options,&b)==ESP_ERR_INVALID_STATE && !b); /* no generation wrap */
    send_error=89;tick();run_job();sa=status(a);
    assert(sa.faulted && !sa.closed && sa.active_sequence==1 && sa.error==89 && sa.lane_identity);
    assert(sa.totals.settled==0 && sa.totals.submitted==0 && live_leases==1);
    esp32_mquickjs_wifi_raw_tx_sessions_request_close();api(release)(a);a=NULL;
    tick();run_job();assert(!esp32_mquickjs_wifi_raw_tx_sessions_drained());
    assert(allocations==frees+4); /* native Session + slots + queue payload + SDK copy retained */
    assert(!critical_depth && !session_mutex_depth);
    /* Inject a separately established physical deinit boundary. This does not
     * test the missing Radio recovery coordinator or authorize production reset. */
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
    assert(s_raw_tx.status.native_terminated && allocations==frees+3);
    tick();run_job();
    assert(esp32_mquickjs_wifi_raw_tx_sessions_drained() && allocations==frees && !s_raw_tx.status.native_terminated);
    return 0;
}
