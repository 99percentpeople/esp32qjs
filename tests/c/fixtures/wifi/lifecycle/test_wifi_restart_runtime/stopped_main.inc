
static void stopped_setup(wifi_mode_t mode) {
    setup_restart();stopped_source=true;stopped_mode=mode;running=sta_storage=false;
    memset(&s_wifi_application,0,sizeof(s_wifi_application));
    memset(&s_wifi_state.radio_lease,0,sizeof(s_wifi_state.radio_lease));
}
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    setup_restart();
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE);
    assert(!result.admitted && !s_wifi_lifecycle.identity && !owner_releases && !temporary);
    stopped_setup(WIFI_MODE_STA);foreign_owner=true;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !result.admitted);
    assert(!s_ap_cleanup_pending && !s_ap_coordinator.identity && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_cleanup_pending=true;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_cleanup_pending && !s_wifi_configuration_cleanup && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_detach_error=77;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_detach_error==77 && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_coordinator.identity=13;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_coordinator.identity==13 && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_lifecycle.identity=17;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_lifecycle.identity==17 && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_lease.acquired && !owner_releases && !temporary);
    stopped_setup(WIFI_MODE_STA);s_wifi_state.scan_future_registered=true;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(!temporary && !s_ap_cleanup_pending);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    stopped_setup(WIFI_MODE_STA);alloc_fail=true;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_NO_MEM && !begin_calls);
    assert(!result.admitted && !s_ap_cleanup_pending && !temporary);
#endif
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        stopped_setup(mode);
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_OK);
        assert(result.admitted && result.resume_attempted && !strcmp(result.stage,"complete"));
        assert(target_mode==mode && phase_calls[10]==1 && phase_calls[1]==1 && phase_calls[9]==1);
        assert(!s_wifi_configuration_cleanup && !s_ap_cleanup_pending && !s_ap_coordinator.identity);
        assert(!s_wifi_lifecycle.identity && !temporary && temporary_frees==CONFIG_ESP_WIFI_SOFTAP_SUPPORT);
        stopped_setup(mode);phase_failure=10;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-110 && result.admitted);
        assert(!checkpoint && !reconstructed && !phase_calls[1] && sta_storage && !temporary);
        assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41);
        unsigned admitted_calls=begin_calls;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !result.admitted);
        assert(begin_calls==admitted_calls);
        phase_failure=0;assert(wifi_finish_configuration_cleanup()==ESP_OK);
        assert(!sta_storage && !s_wifi_lifecycle.identity && !s_ap_coordinator.identity);
    }
    return 0;
}
