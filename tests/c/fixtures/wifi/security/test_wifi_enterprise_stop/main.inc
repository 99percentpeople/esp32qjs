
int main(void){
    for(int stage=0;stage<=4;stage++){
        s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
        s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
        s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
        s_ap_netif=(void *)1;sta_storage=running=true;expect_stop_only=true;eap_binding=123;
        owner_releases=clear_calls=config_begins=config_finishes=0;
        s_wifi_state.status.connected=true;
        assert(wifi_begin_enterprise_stop(eap_binding)==ESP_ERR_INVALID_STATE && !config_begins);
        s_wifi_state.status.connected=false;config_busy=true;
        assert(wifi_begin_enterprise_stop(eap_binding)==ESP_ERR_INVALID_STATE && !config_begins);
        config_busy=false;foreign_owner=true;
        assert(wifi_begin_enterprise_stop(eap_binding)==ESP_ERR_INVALID_STATE);
        assert(!config_busy && config_begins==1 && config_finishes==1 && !owner_releases);
        foreign_owner=false;
        assert(wifi_begin_enterprise_stop(eap_binding)==ESP_OK);
        assert(config_busy && s_wifi_configuration_stop_only && s_ap_coordinator.identity && s_ap_lease.acquired && !owner_releases);
        esp32_mquickjs_wifi_radio_lifecycle_t stale=s_wifi_lifecycle;++stale.identity;
        assert(esp32_mquickjs_wifi_ap_release_enterprise_stop(&stale)==ESP_ERR_INVALID_STATE && s_ap_lease.acquired);
        clear_error=82;
        assert(wifi_finish_configuration_cleanup()==82 && config_busy && !owner_releases);
        assert(eap_binding && s_wifi_eap_stop.binding && s_wifi_lifecycle.identity);
        assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-enterprise-clear"));
        clear_error=0;fail_at=stage;
        int result=wifi_finish_configuration_cleanup();
        assert(result==(stage?-stage:0));
        assert(!eap_binding && !s_wifi_eap_stop.binding && owner_releases==3 && clear_calls==2);
        if(stage){
            assert(config_busy && s_wifi_eap_stop.control.identity && s_wifi_lifecycle.identity);
            if(stage>=3)assert(!s_ap_coordinator.identity); /* Completed AP suffix must not be repeated. */
            fail_at=0;assert(wifi_finish_configuration_cleanup()==ESP_OK && clear_calls==2);
        }
        assert(!config_busy && !s_wifi_eap_stop.control.identity && config_finishes==2);
        assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !s_ap_coordinator.identity);
    }
    /* Exercise the actual public stop helper, including joining its retained
     * transaction rather than rejecting its own busy config token on retry. */
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_ap_netif=(void *)1;sta_storage=running=true;eap_binding=124;
    config_busy=true;assert(wifi_stop_idle()==ESP_ERR_INVALID_STATE);config_busy=false;
    s_wifi_state.status.connected=true;assert(wifi_stop_idle()==ESP_ERR_INVALID_STATE);
    s_wifi_state.status.connected=false;clear_error=83;
    assert(wifi_stop_idle()==83 && config_busy && s_wifi_lifecycle.identity);
    clear_error=0;assert(wifi_stop_idle()==ESP_OK && !config_busy && !eap_binding);
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_ap_netif=(void *)1;sta_storage=running=true;eap_binding=125;clear_error=84;
    assert(wifi_stop_idle()==84 && config_busy);
    assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && config_busy && config_closing);
    clear_error=0;
    assert(esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && !config_busy && !eap_binding);
    assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
    return 0;
}
