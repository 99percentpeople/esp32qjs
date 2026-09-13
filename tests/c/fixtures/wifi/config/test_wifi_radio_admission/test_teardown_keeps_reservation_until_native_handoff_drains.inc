
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    s_wifi_state.connect_timeout_timer=(void *)1;
    wifi_config_t config={.sta.ssid="network",.sta.pmf_cfg.capable=true};
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(admissions==1 && !operation_releases && s_wifi_state.radio_operation.identity);
    assert(wifi_finish_runtime_cleanup(false)==ESP_ERR_INVALID_STATE);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&done);
    const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&fence);
    assert(wifi_finish_runtime_cleanup(true)==ESP_OK);
    assert(admissions==1 && operation_releases==1 && connects==1);
    assert(!s_wifi_state.connect_draining && !s_wifi_state.connect_in_progress);
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    assert(admissions==2 && connects==2);
}
