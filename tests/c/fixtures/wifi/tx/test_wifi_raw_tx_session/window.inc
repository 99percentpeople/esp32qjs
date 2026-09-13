int main(void) {
    (void)start_callback;(void)finish_callback;(void)run_job_thread;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    options_t options={.channel=6,.driver_sequence=true,.capacity=4,.capacity_bytes=96,.max_in_flight=3};
    session_t *a=NULL;options_t invalid=options;invalid.max_in_flight=9;
    assert(api(new)(&invalid,&a)==ESP_ERR_INVALID_ARG && !a);
    assert(!api(new)(&options,&a));tick();run_job();
    enqueue(a,3);flush_t fence={0};flush_status_t snapshot;
    assert(api(flush_begin)(a,&fence)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK);
    tick();run_job();assert(sends==3 && status(a).in_flight==3 && status(a).used_bytes==72);
    enqueue(a,1);assert(status(a).used_bytes==96);
    esp_80211_tx_info_t info;
    esp32qjs_raw_tx_info(&descriptors[2],&info);tick();run_job();
    assert(status(a).in_flight==2 && status(a).used_bytes==72);
    assert(api(flush_status)(a,&fence,&snapshot) && snapshot.pending==2);
    tick();run_job();assert(sends==4 && status(a).in_flight==3);
    esp32qjs_raw_tx_info(&descriptors[0],&info);
    esp32qjs_raw_tx_info(&descriptors[1],&info);tick();run_job();
    assert(api(flush_status)(a,&fence,&snapshot) && !snapshot.pending && snapshot.totals.succeeded==3);
    assert(status(a).in_flight==1 && status(a).used_bytes==24);
    assert(api(flush_release)(a,&fence));
    api(request_close)(a);tick();run_job();assert(!status(a).closed && status(a).in_flight==1);
    esp32qjs_raw_tx_info(&descriptors[3],&info);tick();run_job();
    assert(status(a).closed && !status(a).used_bytes);api(release)(a);assert(allocations==frees);
    return 0;
}
