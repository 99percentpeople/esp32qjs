
int main(void) {
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    esp32_mquickjs_wifi_radio_configuration_selection_t selection={0};
    s_wifi_state.status.connected=true;
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_ERR_INVALID_STATE && !begin_calls);
    selection.allow_disconnect=true;s_wifi_state.scan_future_registered=true;
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_ERR_INVALID_STATE && !begin_calls);
    s_wifi_state.scan_future_registered=false;selection_rejected=true;
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_ERR_INVALID_STATE && !owner_releases);
    assert(selection_allow_disconnect && !s_wifi_configuration_cleanup && !s_ap_cleanup_pending && !s_wifi_lifecycle.identity);
    selection_rejected=false;
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_OK && owner_releases==1);
    assert(s_wifi_configuration_cleanup && s_ap_cleanup_pending && s_wifi_configuration_mode==WIFI_MODE_AP);
    assert(s_wifi_configuration_disconnect_pending && s_wifi_state.radio_lease.acquired && s_wifi_application.acquired);
    assert(wifi_begin_selected_configuration(&selection,NULL,NULL)==ESP_ERR_INVALID_STATE && owner_releases==1);
    return 0;
}
