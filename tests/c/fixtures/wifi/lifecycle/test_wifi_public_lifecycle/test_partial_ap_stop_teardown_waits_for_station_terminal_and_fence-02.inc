
int main(void) {
    s_wifi_ap_stop_cleanup=true;s_wifi_lifecycle.identity=41;
    s_wifi_state.initialized=true;s_wifi_state.started=true;s_wifi_state.status.connected=true;
    s_wifi_application.acquired=true;s_wifi_state.radio_lease.acquired=true;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(!s_wifi_runtime && disconnect_calls==1 && !ap_adoptions && !configuration_cleanup_calls);
    assert(s_wifi_application.acquired && s_wifi_state.radio_lease.acquired && s_wifi_ap_stop_cleanup);
    const esp32_mquickjs_wifi_driver_event_t disconnected={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&disconnected);
    assert(wifi_finish_runtime_cleanup(false)==ESP_ERR_INVALID_STATE && !ap_adoptions && posts==1);
    const esp32_mquickjs_wifi_driver_event_t drained={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&drained);
    configuration_cleanup_error=-8;
    assert(wifi_finish_runtime_cleanup(false)==-8 && ap_adoptions==1 && configuration_cleanup_calls==1);
    assert(!s_wifi_ap_stop_cleanup && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41);
    assert(!s_wifi_configuration_stop_only && !s_wifi_configuration_disconnect_pending);
    configuration_cleanup_error=0;
    assert(wifi_finish_runtime_cleanup(false)==0 && ap_adoptions==1 && configuration_cleanup_calls==2);
    assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_lifecycle.identity && disconnect_calls==1);
}
