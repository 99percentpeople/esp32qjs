
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.started=true;s_wifi_state.connect_in_progress=true;
    assert(wifi_request_disconnect()==ESP_OK);
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&down);
    esp32_mquickjs_future_driver_state_t disconnect={.kind=WIFI_FUTURE_DISCONNECT};
    assert(wifi_future_start(NULL,NULL,9,&disconnect));
    assert(disconnect.completed && s_wifi_state.connect_draining && disconnect_calls==1);
}
