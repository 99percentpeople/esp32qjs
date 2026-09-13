
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.connect_generation=7;
    s_wifi_state.connect_future_registered=true;s_wifi_state.connect_in_progress=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(!s_wifi_runtime && s_wifi_state.connect_draining && disconnect_calls==1);
    assert(!s_wifi_state.connect_future_registered && s_wifi_state.connect_generation==8);
    const esp32_mquickjs_wifi_driver_event_t ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
    wifi_process_driver_event(&ip);assert(!s_wifi_state.status.connected);
    s_wifi_state.connect_future_registered=true;s_wifi_state.connect_future_token=9;
    assert(esp32_mquickjs_wifi_cancel_connect(7)==ESP_OK);
    assert(s_wifi_state.connect_future_registered && s_wifi_state.connect_future_token==9 && disconnect_calls==1);
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    assert(!wifi_future_start(NULL,NULL,10,&scan) && !starts);
}
