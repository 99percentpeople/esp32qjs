
static void check_kind(esp32_mquickjs_wifi_recovery_kind_t kind) {
    original.kind=kind;
    esp32_mquickjs_wifi_recovery_t state={0};bool complete;
    setup_restart();recovery_mode=WIFI_MODE_STA;
    esp32_mquickjs_wifi_recovery_request_t invalid=original;invalid.kind=99;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&invalid,false)==ESP_ERR_INVALID_ARG);
    assert(!begin_calls && !temporary && !owner_releases);
    s_wifi_state.status.connected=true;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_ERR_INVALID_STATE);
    assert(!begin_calls && !temporary && s_wifi_application.acquired);
    s_wifi_state.status.connected=false;s_wifi_state.scan_future_registered=true;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,true)==ESP_ERR_INVALID_STATE);
    s_wifi_state.scan_future_registered=false;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    alloc_fail=true;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_ERR_NO_MEM && !begin_calls);
    alloc_fail=false;
#endif
    esp32_mquickjs_wifi_recovery_request_t wrong_kind=original;
    wrong_kind.kind=kind==ESP32_MQUICKJS_WIFI_RECOVERY_ACTION?ESP32_MQUICKJS_WIFI_RECOVERY_FTM:ESP32_MQUICKJS_WIFI_RECOVERY_ACTION;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&wrong_kind,false)==ESP_ERR_INVALID_STATE);
    assert(!state.execution.admitted && !temporary && !owner_releases);
    esp32_mquickjs_wifi_recovery_request_t stale=original;++stale.identity;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&stale,false)==ESP_ERR_INVALID_STATE);
    assert(!state.execution.admitted && !temporary && !owner_releases);
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?3:1);++mode) {
        setup_restart();recovery_mode=mode;recovery_owner_pending=true;
        recovery_shutdown_calls=recovery_handoffs=0;
        if(mode&WIFI_MODE_AP){s_ap_netif=(void *)1;s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};}
        if(mode==WIFI_MODE_AP && original.kind==ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX) {
            s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){0};
            s_ap_raw_owner.generation=9;s_ap_raw_owner.identity=73;
        }
        assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_OK);
        assert(state.execution.admitted && s_wifi_lifecycle.identity==41 && s_wifi_configuration_cleanup);
        assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_ERR_INVALID_ARG);
        for(unsigned i=0;i<4;++i) {
            assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
            if(i==0)assert(running && checkpoint && !stop_calls);
        }
        assert(!running && !sta_storage && !s_ap_netif && !s_ap_raw_owner.identity);
        assert(phase_calls[1]==1 && stop_calls==1 && !phase_calls[2]);
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
        assert(recovery_shutdown_calls==2 && !recovery_handoffs && !phase_calls[2]);
        assert(!strcmp(state.execution.stage,"recovery-native-drain"));
        /* Only this injected native owner boundary consumes physical proof. */
        recovery_owner_pending=false;
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
        assert(recovery_handoffs==1 && !recovery_admitted && s_wifi_lifecycle.identity==41);
        assert(s_wifi_state.runtime_cleanup_pending && s_wifi_state.cleanup_stage);
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && complete);
        assert(state.execution.resume_attempted && !s_wifi_lifecycle.identity && !s_wifi_configuration_cleanup);
        assert(phase_calls[1]==1 && phase_calls[2]==1 && phase_calls[5]==1 && phase_calls[9]==1);
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && complete && phase_calls[2]==1);
        esp32_mquickjs_wifi_recovery_dispose(&state);
        assert(!state.phase && !state.lifecycle.identity && !temporary);
    }
}
int main(void) {
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
    return 0;
}
