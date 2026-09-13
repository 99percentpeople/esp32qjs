
int main(void) {
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_wifi_state.status.connected=true;
    assert(wifi_begin_configuration_cleanup(false)==ESP_ERR_INVALID_STATE && !begin_calls && !owner_releases);
    s_wifi_state.connect_future_registered=true;
    assert(wifi_begin_configuration_cleanup(true)==ESP_ERR_INVALID_STATE && !begin_calls);
    s_wifi_state.connect_future_registered=false;foreign_owner=true;
    assert(wifi_begin_configuration_cleanup(true)==ESP_ERR_INVALID_STATE && !owner_releases);
    assert(!s_wifi_configuration_cleanup && !s_wifi_configuration_disconnect_pending && s_ap_lease.acquired);
    foreign_owner=false;
    assert(wifi_begin_configuration_cleanup(true)==ESP_OK && owner_releases==1 && s_wifi_configuration_disconnect_pending);
    assert(s_wifi_state.radio_lease.acquired && s_wifi_application.acquired && s_wifi_lifecycle.identity);
    assert(!stop_calls && !ap_calls && !sta_calls && !shutdown_calls);
    return 0;
}
