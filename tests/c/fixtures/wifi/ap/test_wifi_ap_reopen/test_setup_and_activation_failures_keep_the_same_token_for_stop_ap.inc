
int main(void) {
    for(reopen_failure=0;reopen_failure<=3;reopen_failure++) {
        setup();s_ap_netif=NULL;memset(&s_ap_lease,0,sizeof(s_ap_lease));
        reopen_begins=prepares=reopen_finishes=aborts=0;
        wifi_config_t config={.ap.channel=1};bool handled=false;
        int err=esp32_mquickjs_wifi_reopen_ap_shared(&config,&handled);
        assert(handled && s_wifi_application.identity==1 && s_wifi_state.radio_lease.identity==2);
        assert(s_wifi_state.status.connected && sta_storage && !sta_calls && !stop_calls && !shutdown_calls);
        if(!reopen_failure) {
            assert(!err && config.ap.channel==6 && s_ap_lease.acquired && s_ap_netif);
            assert(!s_wifi_lifecycle.identity && !s_ap_coordinator.identity && !s_ap_cleanup_pending && !s_wifi_ap_stop_cleanup);
            assert(reopen_finishes==1 && !aborts);
            /* Reset only fixture AP state before the next independent case. */
            s_ap_netif=NULL;memset(&s_ap_lease,0,sizeof(s_ap_lease));
        } else if(reopen_failure==1) {
            assert(err==-11 && !prepares && !reopen_finishes && !aborts && !s_ap_lease.acquired);
            assert(!s_ap_cleanup_pending && !s_wifi_ap_stop_cleanup && !s_wifi_lifecycle.identity);
        } else {
            assert(err==-10-reopen_failure && aborts==1 && s_wifi_ap_stop_cleanup && s_ap_partial_stop);
            assert(s_wifi_lifecycle.identity==41 && s_ap_coordinator.identity==41 && s_ap_lease.acquired);
            assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123);
            assert(!s_wifi_lifecycle.identity && !s_ap_coordinator.identity && !s_ap_netif && !s_ap_lease.acquired);
            assert(s_wifi_state.status.connected && sta_storage && !sta_calls && !stop_calls);
        }
    }
}
