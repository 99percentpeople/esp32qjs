
int main(void) {
    setup();partial_fail=1;assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);
    esp32_mquickjs_wifi_radio_lifecycle_t stale=s_wifi_lifecycle;stale.generation++;
    assert(esp32_mquickjs_wifi_ap_retire_partial_stop(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_ap_finish_partial_stop(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_ap_adopt_partial_stop(&stale)==ESP_ERR_INVALID_STATE && !adoptions);
    partial_fail=0;s_ap_detach_error=-77;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99 && !ap_calls);
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99 && !ap_calls);
    assert(s_ap_netif && s_ap_lease.acquired && s_wifi_ap_stop_cleanup && s_wifi_state.status.connected);
    s_wifi_state.status.connected=false;
    assert(wifi_stop_idle()==-77 && adoptions==1 && stop_calls==1 && !sta_calls);
    assert(s_ap_netif && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41);
    assert(wifi_stop_idle()==-77 && adoptions==1 && stop_calls==1 && !shutdown_calls);
}
