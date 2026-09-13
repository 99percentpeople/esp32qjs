
int main(void) {
    s_wifi_state.connect_in_progress=true;disconnect_error=-8;
    assert(wifi_request_disconnect()==-8);
    assert(s_wifi_state.connect_draining && s_wifi_state.disconnect_cleanup_error==-8 && !posts);
    disconnect_error=0;assert(wifi_request_disconnect()==ESP_OK);
    assert(wifi_request_disconnect()==ESP_OK && disconnect_calls==2 && !posts);
    netif_up=true;
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&down);
    assert(s_wifi_state.connect_draining && !posts);
    assert(s_wifi_state.disconnect_cleanup_error==ESP_ERR_INVALID_STATE);
    netif_up=false;assert(wifi_request_disconnect()==ESP_OK);
    assert(posts==1 && disconnect_calls==2 && s_wifi_state.connect_draining);
}
