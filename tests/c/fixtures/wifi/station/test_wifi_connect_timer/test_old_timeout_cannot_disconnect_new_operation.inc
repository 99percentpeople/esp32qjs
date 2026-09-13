
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_in_progress=true;
    s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    const esp32_mquickjs_wifi_driver_event_t old={.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,.generation=7};
    wifi_process_driver_event(&old);
    assert(disconnects==0 && s_wifi_state.connect_in_progress);
    const esp32_mquickjs_wifi_driver_event_t current={.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,.generation=8};
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    wifi_process_driver_event(&current);
    assert(disconnects==0 && s_wifi_state.connect_in_progress);
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    wifi_process_driver_event(&current);
    assert(disconnects==1 && !s_wifi_state.connect_in_progress);
}
