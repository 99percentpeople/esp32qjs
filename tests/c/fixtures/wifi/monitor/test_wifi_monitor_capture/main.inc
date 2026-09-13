
static void setup(bool fixed) {
    assert(!pool_allocations);
    memset(&capture,0,sizeof(capture));memset(&resources,0,sizeof(resources));memset(&queue,0,sizeof(queue));
    bridge=(esp32_mquickjs_wifi_monitor_queue_t){&resources,&queue};
    fail_stage=0;hold_rx=hold_radio=subscribe_retained=channel_conflict=cancel_start=false;ps_none=true;cleanup_error=0;
    acquires=subscribes=channel_releases=radio_releases=detaches=discard_calls=0;
    esp32_mquickjs_wifi_monitor_allocator_t allocator={allocate_zero,allocate,free_pool,NULL};
    assert(esp32_mquickjs_wifi_monitor_resources_init(&resources,1,2,24,false,&allocator,publish,NULL));
    esp32_mquickjs_wifi_monitor_capture_options_t options={.filter={.type_mask=4,.sample_every=1,.valid_only=true},.channel=fixed?6:0};
    assert(esp32_mquickjs_wifi_monitor_capture_init(&capture,&resources,&bridge,&options)==ESP_OK);
    assert(esp32_mquickjs_wifi_monitor_capture_init(&capture,&resources,&bridge,&options)==ESP_ERR_INVALID_STATE);
}
static void cleanup(void) {
    fail_stage=0;hold_rx=hold_radio=false;cleanup_error=0;
    assert(esp32_mquickjs_wifi_monitor_capture_close(&capture)==ESP_OK);
    assert(capture.state==ESP32_MQUICKJS_WIFI_MONITOR_CLOSED && !capture.radio.acquired && !capture.promiscuous.acquired);
    assert(!bridge.queue && !queue.has_event && !active_sink);
    assert(esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(!pthread_mutex_destroy(&resources.lock));
}
static void failures(void) {
    for(unsigned stage=1;stage<=6;stage++) {
        setup(true);fail_stage=stage;
        assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==-(int)(10+stage));
        assert(capture.last_error==-(int)(10+stage) && capture.last_stage);
        assert(capture.state==ESP32_MQUICKJS_WIFI_MONITOR_STOPPED && !capture.promiscuous.acquired && !capture.channel_claimed);
        assert(subscribes==(stage==6?1U:0U));cleanup();
    }
    setup(false);ps_none=false;capture.options.require_power_save_none=true;
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    assert(!strcmp(capture.last_stage,"power-save-policy") && !subscribes);cleanup();
    setup(true);fail_stage=6;subscribe_retained=hold_rx=true;cleanup_error=-88;
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==-16);
    assert(capture.last_error==-16 && capture.cleanup_error==-88 && capture.promiscuous.acquired && capture.channel_claimed);
    assert(!channel_releases && !discard_calls && !radio_releases);
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_monitor_capture_close(&capture)==-88 && !detaches);
    cleanup();assert(channel_releases==1 && radio_releases==1 && detaches==1);
}
static void drain_and_retained_frame(void) {
    setup(true);assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_OK && queue.has_event);
    assert(capture.radio_generation==7 && capture.channel_generation==8 && capture.effective_channel==6);
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_OK && acquires==1 && subscribes==1);
    esp32_mquickjs_wifi_monitor_event_t event=queue.event;queue.has_event=false;
    assert(esp32_mquickjs_wifi_monitor_take_event(&resources,&event));
    esp32_mquickjs_wifi_monitor_ref_t ref={0};assert(esp32_mquickjs_wifi_monitor_retain_frame(&resources,&event,&ref));
    hold_rx=true;
    assert(esp32_mquickjs_wifi_monitor_capture_stop(&capture)==ESP_ERR_TIMEOUT);
    assert(capture.state==ESP32_MQUICKJS_WIFI_MONITOR_STOPPING && !capture.cleanup_error && !capture.cleanup_stage);
    active_sink(sink_context,&view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,11);
    assert(!queue.has_event && capture.channel_claimed && !channel_releases);
    hold_rx=false;assert(esp32_mquickjs_wifi_monitor_capture_stop(&capture)==ESP_OK && capture.radio.acquired);
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_OK && subscribes==2 && acquires==1);
    hold_radio=true;
    assert(esp32_mquickjs_wifi_monitor_capture_close(&capture)==ESP_ERR_TIMEOUT && bridge.queue && !detaches);
    hold_radio=false;assert(esp32_mquickjs_wifi_monitor_capture_close(&capture)==ESP_OK);
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(esp32_mquickjs_wifi_monitor_close_frame(&resources,&event));
    const uint8_t *data;size_t length;
    assert(esp32_mquickjs_wifi_monitor_ref_data(&ref,&data,&length) && length==24 && data[0]==8);
    assert(esp32_mquickjs_wifi_monitor_release_ref(&ref));cleanup();
}
static void callback_stop_and_cancel(void) {
    setup(true);assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_OK);
    channel_conflict=true;active_sink(sink_context,&view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,12);
    assert(atomic_load(&capture.channel_conflicted) && atomic_load(&capture.stop_requested));
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    cleanup();
    setup(false);cancel_start=true;
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    assert(!strcmp(capture.last_stage,"start-cancelled") && !queue.has_event);
    assert(atomic_load(&capture.close_requested));cleanup();
}
int main(void) {
    wifi_pkt_rx_ctrl_t rx={0};rx.sig_len=24;rx.channel=6;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx.dump_len=24;
#endif
    uint8_t packet[sizeof(rx)+24];memcpy(packet,&rx,sizeof(rx));memset(packet+sizeof(rx),0,24);packet[sizeof(rx)]=8;
    assert(esp32_mquickjs_wifi_rx_target_view(packet,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
    failures();drain_and_retained_frame();callback_stop_and_cancel();
    assert(!critical_depth && !pool_allocations);
}
