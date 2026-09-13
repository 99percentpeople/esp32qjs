
static void runtime_tick(void) {
    fake_now+=100001;(void)esp32_mquickjs_wifi_raw_tx_sessions_runtime_service();
}
static session_t *open_ap(void) {
    options_t options={.interface=ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT,.channel=6,.driver_sequence=true,
        .capacity=4,.rate_set=true,.rate={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M}};
    session_t *session=NULL;assert(!api(new)(&options,&session));
    unsigned opens=ap_opens;tick();run_job();
    assert(!status(session).open_complete && !ap_helper_live && ap_opens==opens);
    tick();assert(!job_count); /* Background service cannot run AP helper. */
    runtime_tick();return session;
}
static void finish_close(session_t *session) {
    for(unsigned i=0;i<5 && !status(session).closed;++i) {
        runtime_tick();tick();while(job_count)run_job();
    }
    assert(status(session).closed && !ap_helper_live && !live_leases);
    api(release)(session);
}
int main(void) {
    (void)run_job_thread;(void)start_callback;(void)finish_callback;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_AP,.tx_status=WIFI_SEND_SUCCESS};
    session_t *a=open_ap();assert(status(a).open_complete && ap_helper_live);
    uint32_t grant=status(a).lane_identity;
    api(request_close)(a);ap_close_error=61;runtime_tick();
    assert(!status(a).closed && status(a).lane_identity==grant && ap_helper_live);
    tick();if(job_count)run_job();assert(status(a).lane_identity==grant && ap_helper_live);
    ap_close_error=0;finish_close(a);assert(allocations==frees);
    /* Failure after helper ownership transfer still closes unpublished Session. */
    ap_open_error=62;a=open_ap();assert(status(a).faulted && ap_helper_live);
    ap_open_error=0;finish_close(a);assert(allocations==frees);
    /* Queued close before opening invokes no helper at all. */
    options_t options={.interface=ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT,.channel=6,.driver_sequence=true,
        .capacity=4,.rate_set=true,.rate={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M}};
    a=NULL;assert(!api(new)(&options,&a));unsigned opened=ap_opens;
    api(request_close)(a);finish_close(a);assert(ap_opens==opened && allocations==frees);
    a=open_ap();assert(status(a).open_complete);enqueue(a,1);tick();run_job();
    assert(status(a).active_sequence);api(request_close)(a);runtime_tick();
    unsigned closes=ap_closes;assert(ap_helper_live);tick();run_job();assert(ap_closes==closes);
    /* Inject separately proven physical deinit after central helper retirement;
     * production broker hands termination to the real Session worker. */
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
    tick();run_job();assert(!live_leases && !status(a).closed && ap_helper_live);
    finish_close(a);assert(allocations==frees);
    return 0;
}
