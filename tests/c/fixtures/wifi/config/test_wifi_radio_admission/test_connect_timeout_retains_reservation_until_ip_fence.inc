
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    s_wifi_state.connect_timeout_timer=(void *)1;
    wifi_config_t config={.sta.ssid="network",.sta.pmf_cfg.capable=true};
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    const esp32_mquickjs_wifi_driver_event_t timeout={.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,.generation=8};
    wifi_process_driver_event(&timeout);wifi_driver_event_poller(NULL,NULL,NULL);
    assert(admissions==1 && !operation_releases && s_wifi_state.connect_draining);
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&down);wifi_driver_event_poller(NULL,NULL,NULL);
    assert(!operation_releases);
    const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&fence);wifi_driver_event_poller(NULL,NULL,NULL);
    assert(operation_releases==1 && !s_wifi_state.radio_operation.identity);
}
