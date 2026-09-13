
int main(void) {
    s_wifi_state.status.connected=true;s_wifi_state.connect_generation=8;
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    wifi_config_t config={.sta.ssid="new-network",.sta.pmf_cfg.capable=true};
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_ERR_TIMEOUT);
    assert(s_wifi_state.connect_draining && disconnect_calls==1 && !configs && !connects);
    deliver_disconnect_on_wait=true;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_OK);
    assert(disconnect_calls==1 && configs==1 && connects==1 && !s_wifi_state.connect_draining);
    assert(s_wifi_state.connect_in_progress && !s_wifi_state.status.connected && !connection_events);
    const esp32_mquickjs_wifi_driver_event_t ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
    wifi_process_driver_event(&ip);
    assert(s_wifi_state.status.connected && !s_wifi_state.connect_in_progress);
    assert(connection_events==1 && last_connection_kind==ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS);
}
