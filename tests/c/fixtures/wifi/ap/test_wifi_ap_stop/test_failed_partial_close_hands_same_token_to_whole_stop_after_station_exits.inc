
int main(void) {
    for(int stage=1;stage<4;stage++) {
        setup();partial_fail=stage;
        assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);
        assert(wifi_stop_idle()==ESP_ERR_INVALID_STATE && !adoptions && !stop_calls && !owner_releases);
        s_wifi_state.status.connected=false;s_wifi_state.connect_draining=true;
        assert(wifi_stop_idle()==ESP_ERR_INVALID_STATE && !adoptions);
        s_wifi_state.connect_draining=false;partial_fail=0;fail_at=1;
        int modes=partial_mutations;
        assert(wifi_stop_idle()==-1);
        assert(adoptions==1 && !s_wifi_ap_stop_cleanup && !s_ap_partial_stop);
        assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41 && s_ap_coordinator.identity==41);
        assert(owner_releases==3 && s_wifi_state.runtime_cleanup_pending && !s_wifi_configuration_stop_only);
        assert(partial_mutations==modes);
        fail_at=0;
        assert(wifi_stop_idle()==0 && adoptions==1 && !s_wifi_lifecycle.identity);
        assert(!s_wifi_configuration_cleanup && !s_wifi_state.runtime_cleanup_pending);
        assert(!s_ap_coordinator.identity && !s_ap_cleanup_pending && !s_ap_netif && !sta_storage);
        assert(owner_releases==3 && shutdown_calls==1 && partial_mutations==modes);
    }
}
