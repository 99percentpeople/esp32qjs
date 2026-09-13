
static void setup_enterprise(bool ap){
    setup_restart();memset(&s_wifi_eap_stop,0,sizeof(s_wifi_eap_stop));
    eap_binding=++next_eap_binding;config_busy=config_closing=false;
    clear_error=reinstall_error=0;clear_calls=config_begins=config_finishes=reinstalls=0;
    retry_allowed=true;ap_release_blocked=false;
    if(ap){s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};s_ap_netif=(void *)1;}
}
#define RESTART(allow) esp32_mquickjs_wifi_restart_stopped_interfaces(allow,&result)
int main(void){
    esp32_mquickjs_wifi_configuration_execution_t result;
    setup_enterprise(true);
    uint64_t original=eap_binding;
    assert(RESTART(false)==ESP_ERR_INVALID_STATE&&!result.admitted&&!s_wifi_lifecycle.identity);
    assert(!clear_calls&&!owner_releases&&!config_busy&&eap_binding==original&&!temporary);
    s_wifi_state.status.connected=true;assert(RESTART(true)==ESP_ERR_INVALID_STATE&&!clear_calls);
    s_wifi_state.status.connected=false;config_closing=true;
    assert(RESTART(true)==ESP_ERR_INVALID_STATE&&!clear_calls);config_closing=false;
    /* A completed SDK clear does not repeat when AP release is still blocked. */
    ap_release_blocked=true;
    assert(RESTART(true)==ESP_ERR_INVALID_STATE&&config_busy&&s_wifi_lifecycle.identity);
    assert(clear_calls==1&&!eap_binding&&!s_wifi_eap_stop.binding&&s_wifi_eap_stop.release_ap&&!phase_calls[1]);
    assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-enterprise-ap-release"));
    ap_release_blocked=false;
    assert(!RESTART(true)&&clear_calls==1&&reinstalls==1&&!config_busy&&!s_wifi_eap_stop.profile);
    assert(running&&s_wifi_application.acquired&&s_wifi_state.radio_lease.acquired&&s_ap_lease.acquired);
    for(int ap=0;ap<=1;++ap){
        setup_enterprise(ap);clear_error=91;
        assert(RESTART(ap)==91&&config_busy&&eap_binding&&s_wifi_eap_stop.profile);
        assert(!phase_calls[1]&&!owner_releases&&!s_wifi_eap_stop.checkpoint_attempted);
        clear_error=0;reinstall_error=92;
        assert(RESTART(ap)==92&&config_busy&&s_wifi_lifecycle.identity&&s_wifi_eap_stop.binding==eap_binding);
        assert(checkpoint&&reconstructed&&replayed&&!s_wifi_eap_stop.release_ap&&!s_ap_coordinator.identity);
        assert(!s_wifi_application.acquired&&!s_wifi_state.radio_lease.acquired&&!s_ap_lease.acquired);
        unsigned clears_before=clear_calls,stops_before=stop_calls;
        clear_error=93;
        assert(RESTART(ap)==93&&clear_calls==clears_before+1&&stop_calls==stops_before);
        assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-enterprise-clear")&&config_busy);
        clear_error=reinstall_error=0;
        assert(!RESTART(ap)&&reinstalls==2&&!config_busy&&!s_wifi_lifecycle.identity);
        assert(phase_calls[1]==1&&phase_calls[2]==2&&eap_binding&&!temporary);
        /* Config close bars reinstall and hands this same operation to cleanup. */
        setup_enterprise(ap);reinstall_error=94;
        assert(RESTART(ap)==94&&config_busy&&s_wifi_eap_stop.binding);
        clear_error=95;
        assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy()&&config_closing&&config_busy);
        unsigned installs_before=reinstalls;
        assert(RESTART(ap)==ESP_ERR_INVALID_STATE&&reinstalls==installs_before);
        clear_error=0;
        assert(esp32_mquickjs_wifi_eap_prepare_runtime_destroy()&&!config_busy&&!eap_binding);
        assert(!s_wifi_lifecycle.identity&&!s_wifi_configuration_cleanup&&!s_wifi_eap_stop.profile);
    }
    /* Incomplete source capture can be retired, but cannot be recaptured. */
    setup_enterprise(false);phase_failure=1;
    assert(RESTART(false)==-101&&s_wifi_eap_stop.checkpoint_attempted);
    checkpoint=false; /* Native capture boundary reports an incomplete snapshot. */
    phase_failure=0;assert(RESTART(false)==ESP_ERR_INVALID_STATE&&phase_calls[1]==1);
    assert(!wifi_finish_configuration_cleanup()&&!config_busy&&!eap_binding&&!s_wifi_lifecycle.identity);
    return 0;
}
