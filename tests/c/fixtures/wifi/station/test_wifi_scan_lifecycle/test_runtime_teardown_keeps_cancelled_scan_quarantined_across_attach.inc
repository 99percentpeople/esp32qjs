
int main(void) {
    s_wifi_state.scan_queue=(void *)1;
    esp32_mquickjs_future_driver_state_t *a=calloc(1,sizeof(*a)),b={.kind=WIFI_FUTURE_SCAN};
    a->kind=WIFI_FUTURE_SCAN;a->scan_config.channel=6;
    assert(wifi_future_start(NULL,NULL,1,a));
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    wifi_future_destroy(a);
    assert(!s_wifi_runtime && !s_wifi_state.scan_future_registered);
    assert(s_wifi_state.scan_in_progress && s_wifi_state.scan_draining && stops==1);
    assert(s_wifi_state.native_scan_config.channel==6);
    assert(wifi_finish_runtime_cleanup(false)==ESP_ERR_INVALID_STATE);
    assert(!wifi_future_start(NULL,NULL,2,&b));
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);
    assert(!queued);
    assert(wifi_finish_runtime_cleanup(true)==ESP_OK);
    s_wifi_runtime=(void *)2;
    assert(wifi_future_start(NULL,NULL,2,&b));
    assert(starts==2 && clears==1);
}
