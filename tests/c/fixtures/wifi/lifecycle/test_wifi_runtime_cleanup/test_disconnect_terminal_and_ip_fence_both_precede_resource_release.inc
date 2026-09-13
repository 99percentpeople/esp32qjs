
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.status.connected=true;
    s_wifi_state.radio_operation.identity=9;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(!cleanup_calls && !operation_releases && disconnect_calls==1);
    const esp32_mquickjs_wifi_driver_event_t disconnected={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&disconnected);
    assert(wifi_finish_runtime_cleanup(false)==ESP_ERR_INVALID_STATE);
    assert(!cleanup_calls && !operation_releases && posts==1);
    const esp32_mquickjs_wifi_driver_event_t drained={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&drained);
    assert(wifi_finish_runtime_cleanup(false)==ESP_OK);
    assert(cleanup_calls==1 && operation_releases==1 && disconnect_calls==1);
    assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_runtime);
}
