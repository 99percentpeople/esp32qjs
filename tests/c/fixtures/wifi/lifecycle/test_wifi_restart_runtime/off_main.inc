
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    for(unsigned ap_policy=0;ap_policy<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?2U:1U);++ap_policy) {
        stopped_setup(WIFI_MODE_NULL);off_source=history_missing=true;off_needs_ap=ap_policy;
        assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(ap_policy,&result));
        assert(result.admitted && result.configuration_attempted && result.resume_attempted);
        assert(!running && !sta_storage && !s_ap_netif && !s_wifi_application.acquired);
        assert(!s_wifi_state.radio_lease.acquired && !s_ap_lease.acquired && !temporary);
        assert(!s_wifi_lifecycle.identity && !native_token.identity && !s_wifi_configuration_cleanup);
        assert(phase_calls[12]==1 && !phase_calls[9] && phase_calls[10]==1);
        assert(phase_calls[11]==ap_policy && target_mode==(ap_policy?WIFI_MODE_APSTA:WIFI_MODE_STA));
        stopped_setup(WIFI_MODE_NULL);off_source=history_missing=true;off_needs_ap=ap_policy;phase_failure=12;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap_policy,&result)==-112);
        assert(s_wifi_lifecycle.identity==41 && checkpoint && !temporary && result.admitted);
        assert(s_wifi_configuration_cleanup && s_wifi_state.runtime_cleanup_pending);
        phase_failure=0;
        assert(wifi_finish_configuration_cleanup()==ESP_OK && !running && !s_wifi_lifecycle.identity);
        assert(!sta_storage && !s_ap_netif && !s_wifi_configuration_cleanup);
        for(unsigned failure=2;failure<=4;++failure) {
            if(failure==2 && !ap_policy)continue;
            stopped_setup(WIFI_MODE_NULL);off_source=history_missing=true;off_needs_ap=ap_policy;
            off_cleanup_failure=failure;
            assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap_policy,&result)==-(int)failure);
            assert(s_wifi_lifecycle.identity==41 && !running && s_wifi_configuration_cleanup);
            assert(!temporary && phase_calls[12]==1 && !phase_calls[9]);
            /* Ordinary central cleanup consumes only remaining helper/shutdown suffix. */
            fail_at=0;expect_stop_only=false;
            assert(!wifi_finish_configuration_cleanup() && !s_wifi_lifecycle.identity && !sta_storage && !s_ap_netif);
            assert(phase_calls[12]==1);
        }
    }
    return 0;
}
