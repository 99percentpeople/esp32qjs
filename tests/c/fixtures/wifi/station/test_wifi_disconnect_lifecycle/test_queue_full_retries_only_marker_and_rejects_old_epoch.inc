
int main(void) {
    s_wifi_state.connect_in_progress=true;post_error=-9;
    s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    assert(wifi_request_disconnect()==ESP_OK);
    const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&down);
    assert(s_wifi_state.connect_draining && !s_wifi_state.disconnect_fence_posted);
    assert(s_wifi_state.disconnect_cleanup_error==-9 && disconnect_calls==1 && posts==1);
    assert(connection_events==1 && last_connection_kind==ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED);
    post_error=0;assert(wifi_request_disconnect()==ESP_OK);
    assert(disconnect_calls==1 && posts==2 && s_wifi_state.connect_draining);
    const esp32_mquickjs_wifi_driver_event_t old={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch-1};
    wifi_process_driver_event(&old);assert(s_wifi_state.connect_draining);
    const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&fence);assert(!s_wifi_state.connect_draining);
    s_wifi_state.connect_in_progress=true;assert(wifi_request_disconnect()==ESP_OK);
    wifi_process_driver_event(&down);wifi_process_driver_event(&fence);
    assert(s_wifi_state.connect_draining && disconnect_calls==2 && posts==3);
}
