
int main(void) {
    setup();s_ap_netif=NULL;memset(&s_ap_lease,0,sizeof(s_ap_lease));
    s_wifi_state.scan_future_registered=true;
    wifi_config_t config={0};bool handled=false;
    assert(esp32_mquickjs_wifi_reopen_ap_shared(&config,&handled)==ESP_ERR_INVALID_STATE && handled);
    assert(!reopen_begins && !s_wifi_lifecycle.identity && !s_wifi_ap_stop_cleanup);
    s_wifi_state.scan_future_registered=false;s_wifi_state.connect_draining=true;
    assert(esp32_mquickjs_wifi_reopen_ap_shared(&config,&handled)==ESP_ERR_INVALID_STATE);
    assert(!reopen_begins && !s_ap_cleanup_pending && s_wifi_state.status.connected);
}
