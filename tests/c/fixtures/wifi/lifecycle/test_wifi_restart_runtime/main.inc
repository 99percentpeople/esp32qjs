
static void setup_restart(void) {
    assert(!temporary);
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(&s_wifi_lifecycle,0,sizeof(s_wifi_lifecycle));
    memset(&s_ap_lifecycle,0,sizeof(s_ap_lifecycle));memset(&s_ap_coordinator,0,sizeof(s_ap_coordinator));
    memset(&native_token,0,sizeof(native_token));memset(phase_calls,0,sizeof(phase_calls));
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){0};
    memset(&s_ap_raw_owner,0,sizeof(s_ap_raw_owner));
    s_ap_netif=NULL;s_ap_detach_error=0;s_ap_stage=NULL;
    s_ap_cleanup_pending=s_wifi_configuration_cleanup=s_wifi_ap_stop_cleanup=false;
    s_wifi_configuration_disconnect_pending=s_wifi_configuration_stop_only=false;
    foreign_owner=alloc_fail=checkpoint=reconstructed=replayed=expect_stop_only=stopped_source=history_missing=off_source=off_needs_ap=false;
    begin_calls=owner_releases=stop_calls=ap_calls=sta_calls=shutdown_calls=0;
    phase_failure=fail_at=temporary_frees=off_cleanup_failure=0;running=sta_storage=true;
    stopped_mode=WIFI_MODE_STA;retry_allowed=false;retry_admissions=0;
}
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    setup_restart();s_wifi_state.status.connected=true;
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_STA,&result)==ESP_ERR_INVALID_STATE && !result.admitted && !begin_calls);
    s_wifi_state.status.connected=false;s_wifi_state.scan_future_registered=true;
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_STA,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    s_wifi_state.scan_future_registered=false;foreign_owner=true;
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_STA,&result)==ESP_ERR_INVALID_STATE && !owner_releases && !result.admitted);
    setup_restart();
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_NULL,&result)==ESP_ERR_INVALID_ARG && !begin_calls);
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_AP,&result)==ESP_ERR_NOT_SUPPORTED && !begin_calls);
#else
    alloc_fail=true;
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_AP,&result)==ESP_ERR_NO_MEM && !begin_calls && !result.admitted);
#endif
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        setup_restart();
        assert(esp32_mquickjs_wifi_restart_interfaces(mode,&result)==ESP_OK && result.admitted && result.resume_attempted);
        assert(!strcmp(result.stage,"complete") && !s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
        assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_state.cleanup_stage && !temporary);
        assert(phase_calls[1]==1 && phase_calls[2]==1 && phase_calls[3]==1 && phase_calls[5]==1 && phase_calls[9]==1);
        assert(phase_calls[6]==(mode==WIFI_MODE_AP) && temporary_frees==((mode&WIFI_MODE_AP)!=0));
        for(int failure=1;failure<=9;++failure) {
            if(!(mode&WIFI_MODE_AP) && (failure==4 || (failure>=6 && failure<=8)))continue;
            if(mode!=WIFI_MODE_AP && failure==6)continue;
            setup_restart();phase_failure=failure;
            int error=esp32_mquickjs_wifi_restart_interfaces(mode,&result);
            assert(error==(failure==8 ? ESP_ERR_INVALID_STATE : -(100+failure)));
            assert(result.admitted && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41 && !temporary);
            assert(s_wifi_state.runtime_cleanup_pending && s_wifi_state.cleanup_error==error);
            assert(!s_wifi_application.acquired && !s_wifi_state.radio_lease.acquired && !s_ap_lease.acquired);
            unsigned previous[13];memcpy(previous,phase_calls,sizeof(previous));
            assert(esp32_mquickjs_wifi_restart_interfaces(mode,&result)==ESP_ERR_INVALID_STATE);
            assert(!result.admitted && !memcmp(previous,phase_calls,sizeof(previous)));
            phase_failure=0;fail_at=4; /* Central shutdown failure retains suffix/token. */
            assert(wifi_finish_configuration_cleanup()==-4 && s_wifi_lifecycle.identity==41 && checkpoint);
            unsigned retired_ap=ap_calls,retired_sta=sta_calls,stopped=stop_calls;
            fail_at=0;assert(wifi_finish_configuration_cleanup()==ESP_OK);
            assert(!s_wifi_configuration_cleanup && !s_wifi_state.runtime_cleanup_pending && !s_wifi_lifecycle.identity);
            assert(ap_calls==retired_ap && sta_calls==retired_sta && stop_calls==stopped);
            assert(!s_ap_netif && !sta_storage && !running && shutdown_calls==2);
        }
    }
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        setup_restart();stopped_source=true;running=sta_storage=false;
        memset(&s_wifi_application,0,sizeof(s_wifi_application));memset(&s_wifi_state.radio_lease,0,sizeof(s_wifi_state.radio_lease));
        assert(esp32_mquickjs_wifi_restart_interfaces(mode,&result)==ESP_OK);
        assert(phase_calls[10]==1 && phase_calls[1]==1 && phase_calls[2]==1 && phase_calls[3]==1);
        assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !temporary);
        setup_restart();stopped_source=true;running=sta_storage=false;phase_failure=10;
        assert(esp32_mquickjs_wifi_restart_interfaces(mode,&result)==-110 && result.admitted);
        assert(!checkpoint && !reconstructed && !phase_calls[1] && sta_storage && !temporary);
        phase_failure=0;assert(wifi_finish_configuration_cleanup()==ESP_OK && !sta_storage && !s_wifi_lifecycle.identity);
    }
    return 0;
}
