int main(void) {
    (void)start_callback;(void)finish_callback;(void)run_job_thread;(void)tick;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    options_t options={.channel=6,.driver_sequence=true,.capacity=4,.max_in_flight=3};
    session_t *a=NULL;timer_fail=true;
    assert(api(new)(&options,&a)==ESP_ERR_NO_MEM && !a && allocations==frees);
    timer_fail=false;assert(!api(new)(&options,&a));drain_pump();assert(status(a).open_complete);
    /* Queue saturation recovery driven only by native timer/worker boundaries. */
    queue_full=true;enqueue(a,4);assert(!job_count && pump_test_timer.active);
    fire_pump();assert(!job_count && pump_test_timer.active && !sends);
    queue_full=false;fire_pump();drain_pump();assert(sends==3 && status(a).in_flight==3);
    esp_80211_tx_info_t info;
    esp32qjs_raw_tx_info(&descriptors[2],&info);drain_pump();
    assert(sends==4 && status(a).in_flight==3); /* Immediate refill, no timer/JS tick. */
    esp32qjs_raw_tx_info(&descriptors[0],&info);esp32qjs_raw_tx_info(&descriptors[1],&info);
    esp32qjs_raw_tx_info(&descriptors[3],&info);drain_pump();
    assert(status(a).totals.succeeded==4 && !status(a).in_flight);
    if(pump_test_timer.active){fire_pump();drain_pump();}
    assert(!pump_test_timer.active); /* Open idle Session does not poll. */
    api(request_close)(a);drain_pump();assert(status(a).closed);api(release)(a);
    if(pump_test_timer.active){fire_pump();drain_pump();}
    assert(!pump_test_timer.active && allocations==frees);
    /* Runtime replacement reuses bounded boot-owned timer, no executor pointer. */
    a=NULL;assert(!api(new)(&options,&a));drain_pump();assert(timer_creates==2);
    api(request_close)(a);drain_pump();assert(status(a).closed);api(release)(a);
    assert(allocations==frees);
    return 0;
}
