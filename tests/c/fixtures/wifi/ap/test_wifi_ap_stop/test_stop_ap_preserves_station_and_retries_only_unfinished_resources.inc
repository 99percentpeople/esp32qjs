
int main(void) {
    for(int stage=0;stage<4;stage++) {
        setup();partial_fail=stage;
        if(stage) {
            assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);
            assert(s_wifi_ap_stop_cleanup && s_ap_partial_stop && s_ap_cleanup_pending);
            assert(s_wifi_lifecycle.identity==41 && s_ap_coordinator.identity==41);
            assert(s_wifi_application.identity==1 && s_wifi_state.radio_lease.identity==2 && s_ap_lease.acquired);
            assert(s_wifi_state.status.connected && sta_storage && !sta_calls && !stop_calls);
            assert(!owner_releases && !s_wifi_state.runtime_cleanup_pending);
            assert(s_ap_netif==(stage==3?NULL:(void *)1));
            partial_fail=0;fail_at=0;
        }
        assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123);
        assert(!s_wifi_ap_stop_cleanup && !s_ap_partial_stop && !s_ap_cleanup_pending);
        assert(!s_wifi_lifecycle.identity && !s_ap_coordinator.identity && !s_ap_lease.acquired && !s_ap_netif);
        assert(s_wifi_application.identity==1 && s_wifi_state.radio_lease.identity==2);
        assert(s_wifi_state.status.connected && sta_storage && running && !sta_calls && !stop_calls && !shutdown_calls);
        assert(owner_releases==1 && partial_admissions==1 && partial_mutations==1);
        int calls=partial_quiesce_calls;
        assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123 && partial_quiesce_calls==calls);
        assert(owner_releases==1 && !stop_calls && !shutdown_calls);
    }
}
