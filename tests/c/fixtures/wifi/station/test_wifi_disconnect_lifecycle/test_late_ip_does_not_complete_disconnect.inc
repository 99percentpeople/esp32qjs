
int main(void) {
    s_wifi_state.connect_future_registered=true;s_wifi_state.connect_generation=7;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    const esp32_mquickjs_wifi_driver_event_t ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
    wifi_process_driver_event(&ip);
    assert(!s_wifi_state.status.connected && !connection_events);
}
