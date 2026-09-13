
int main(void) {
    s_wifi_state.disconnect_epoch=UINT32_MAX;s_wifi_state.connect_in_progress=true;
    (void)wifi_request_disconnect();
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    const esp32_mquickjs_wifi_driver_event_t old={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=UINT32_MAX};
    wifi_process_driver_event(&down);wifi_process_driver_event(&old);
    assert(s_wifi_state.disconnect_epoch==UINT32_MAX && s_wifi_state.disconnect_epoch_exhausted);
    assert(s_wifi_state.connect_draining && !posts && disconnect_calls==1);
    assert(wifi_request_disconnect()==ESP_ERR_INVALID_STATE);
}
