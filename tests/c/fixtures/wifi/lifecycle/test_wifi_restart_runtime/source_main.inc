
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        stopped_setup(mode);history_missing=true;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_OK);
        assert(result.admitted && result.resume_attempted && phase_calls[1]==1);
        assert(phase_calls[11]==((mode&WIFI_MODE_AP)!=0) && phase_calls[10]==1);
        assert(!temporary && !s_wifi_lifecycle.identity && !s_wifi_configuration_cleanup);
        if(!(mode&WIFI_MODE_AP))continue;
        stopped_setup(mode);history_missing=true;phase_failure=11;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-111);
        assert(result.admitted && !result.stop_attempted && !strcmp(result.stage,"restart-stopped-ap-prepare"));
        assert(!checkpoint && !reconstructed && !phase_calls[1] && !temporary);
        assert(sta_storage && s_ap_netif && s_ap_cleanup_pending && s_wifi_lifecycle.identity==41);
        phase_failure=0;assert(wifi_finish_configuration_cleanup()==ESP_OK);
        assert(!sta_storage && !s_ap_netif && !s_wifi_lifecycle.identity);
    }
    return 0;
}
