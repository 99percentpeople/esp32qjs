
int main(void) {
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_ap_netif=(void *)1;sta_storage=running=true;
    assert(wifi_begin_configuration_cleanup(false)==ESP_OK);
    /* Inject the admitted Radio identity, not a replacement cleanup FSM. */
    recovery_admitted=recovery_owner_pending=true;
    esp32_mquickjs_wifi_radio_lifecycle_t stale=s_wifi_lifecycle;++stale.identity;
    assert(esp32_mquickjs_wifi_ap_retire_for_recovery(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_retire_for_recovery(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_ap_retire_for_recovery(&s_wifi_lifecycle)==ESP_ERR_INVALID_STATE);
    fail_at=2;
    assert(wifi_finish_configuration_cleanup()==-2 && s_ap_netif && sta_storage);
    assert(!recovery_shutdown_calls && stop_calls==1 && s_ap_coordinator.identity==41);
    fail_at=0;
    assert(wifi_finish_configuration_cleanup()==ESP_ERR_TIMEOUT);
    assert(s_wifi_lifecycle.identity==41 && s_wifi_configuration_cleanup && s_wifi_state.runtime_cleanup_pending);
    assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-recovery-shutdown"));
    assert(!s_ap_netif && !sta_storage && !s_ap_coordinator.identity);
    assert(stop_calls==1 && ap_calls==2 && sta_calls==1 && !shutdown_calls);
    assert(wifi_finish_configuration_cleanup()==ESP_ERR_TIMEOUT);
    assert(stop_calls==1 && ap_calls==2 && sta_calls==1 && recovery_shutdown_calls==2);
    recovery_owner_pending=false;
    assert(wifi_finish_configuration_cleanup()==ESP_OK && shutdown_calls==1);
    assert(!s_wifi_lifecycle.identity && !s_wifi_configuration_cleanup && !s_wifi_state.runtime_cleanup_pending);
    assert(stop_calls==1 && ap_calls==2 && sta_calls==1 && recovery_shutdown_calls==3);
    return 0;
}
