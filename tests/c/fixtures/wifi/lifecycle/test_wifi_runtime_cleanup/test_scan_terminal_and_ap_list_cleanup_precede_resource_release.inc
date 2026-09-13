
int main(void) {
    esp32_mquickjs_future_driver_state_t a={.kind=WIFI_FUTURE_SCAN};
    assert(wifi_future_start(NULL,NULL,1,&a));
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(stops==1 && !cleanup_calls && !clears && !operation_releases);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);clear_error=-9;
    assert(wifi_finish_runtime_cleanup(true)==-9);
    assert(!ticks && !cleanup_calls && clears==1 && !operation_releases);
    assert(!strcmp(s_wifi_state.cleanup_stage,"scan-drain"));
    clear_error=0;
    assert(wifi_finish_runtime_cleanup(true)==ESP_OK);
    assert(stops==1 && clears==2 && cleanup_calls==1 && operation_releases==1);
}
