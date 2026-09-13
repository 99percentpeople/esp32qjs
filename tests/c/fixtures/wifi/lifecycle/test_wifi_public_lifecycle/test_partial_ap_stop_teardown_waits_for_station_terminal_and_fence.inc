
static bool s_wifi_configuration_disconnect_pending;
static int ap_adoptions;
static int esp32_mquickjs_wifi_ap_adopt_partial_stop(const void *token) {
    assert(token==&s_wifi_lifecycle && s_wifi_lifecycle.identity==41);
    assert(!s_wifi_runtime && !s_wifi_state.status.connected && !s_wifi_state.connect_draining);
    assert(!s_wifi_state.disconnect_active && s_wifi_application.acquired && s_wifi_state.radio_lease.acquired);
    ap_adoptions++;return 0;
}
