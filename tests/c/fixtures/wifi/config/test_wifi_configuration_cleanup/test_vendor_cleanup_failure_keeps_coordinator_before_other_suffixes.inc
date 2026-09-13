
int main(void) {
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    assert(wifi_begin_configuration_cleanup(false)==ESP_OK);
    unsigned releases=owner_releases;vendor_clear_error=77;
    assert(wifi_finish_configuration_cleanup()==77 && s_wifi_lifecycle.identity==41);
    assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-vendor-ie-clear") && owner_releases==releases);
    assert(!stop_calls && !ap_calls && !sta_calls && !shutdown_calls);
    vendor_clear_error=0;assert(wifi_finish_configuration_cleanup()==ESP_OK && !s_wifi_lifecycle.identity);
    return 0;
}
