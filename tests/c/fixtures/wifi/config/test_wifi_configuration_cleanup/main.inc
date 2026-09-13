
int main(void) {
    for(int stop_only=0;stop_only<=1;stop_only++)for(int stage=1;stage<=4;stage++) {
        expect_stop_only=stop_only;
        s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
        s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
        s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
        s_ap_netif=(void *)1;sta_storage=running=true;
        begin_calls=owner_releases=stop_calls=ap_calls=sta_calls=shutdown_calls=0;
        s_wifi_state.connect_future_registered=true;
        assert(wifi_begin_configuration_cleanup(false)==ESP_ERR_INVALID_STATE && !begin_calls);
        s_wifi_state.connect_future_registered=false;foreign_owner=true;
        assert(wifi_begin_configuration_cleanup(false)==ESP_ERR_INVALID_STATE);
        assert(!owner_releases && s_ap_lease.acquired && !s_ap_coordinator.identity && !s_wifi_configuration_cleanup);
        foreign_owner=false;
        assert(wifi_begin_configuration_cleanup(false)==0 && s_wifi_configuration_cleanup && !s_wifi_configuration_stop_only);
        s_wifi_configuration_stop_only=stop_only;
        assert(begin_calls==2 && owner_releases==1 && !s_ap_lease.acquired && s_ap_coordinator.identity==41);
        assert(wifi_ap_cleanup()==ESP_ERR_INVALID_STATE && !stop_calls);
        esp32_mquickjs_wifi_radio_lifecycle_t stale={9,40};
        assert(esp32_mquickjs_wifi_ap_retire_for_configuration(&stale)==ESP_ERR_INVALID_STATE && s_ap_netif);
        fail_at=stage;
        assert(wifi_finish_configuration_cleanup()==-stage);
        assert(s_wifi_configuration_cleanup && s_wifi_state.runtime_cleanup_pending && s_wifi_lifecycle.identity==41);
        assert(s_wifi_configuration_stop_only==stop_only);
        if(stage==4)assert(!strcmp(s_wifi_state.cleanup_stage,stop_only?"configuration-finish-stop":"configuration-shutdown"));
        assert(owner_releases==3 && begin_calls==2);
        assert(ap_calls==(stage>=2) && sta_calls==(stage>=3) && shutdown_calls==(stage==4));
        if(stage<=2)assert(s_ap_netif && s_ap_coordinator.identity==41);
        else assert(!s_ap_netif && !s_ap_coordinator.identity);
        fail_at=0;
        assert(wifi_begin_configuration_cleanup(false)==0 && begin_calls==2);
        assert(wifi_finish_configuration_cleanup()==0);
        assert(!s_wifi_configuration_cleanup && !s_wifi_state.runtime_cleanup_pending && !s_wifi_lifecycle.identity && !s_wifi_configuration_stop_only);
        assert(!s_wifi_state.cleanup_stage && !s_wifi_state.cleanup_error && !s_ap_netif && !sta_storage);
        assert(stop_calls==1+(stage==1) && ap_calls==1+(stage==2) && sta_calls==1+(stage==3));
        assert(shutdown_calls==1+(stage==4) && owner_releases==3 && begin_calls==2);
        assert(wifi_finish_configuration_cleanup()==ESP_ERR_INVALID_STATE);
    }
    return 0;
}
