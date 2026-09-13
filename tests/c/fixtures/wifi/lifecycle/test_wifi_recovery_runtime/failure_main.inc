
static void check_kind(esp32_mquickjs_wifi_recovery_kind_t kind) {
    original.kind=kind;
    for(unsigned point=0;point<6;++point) {
        setup_restart();recovery_mode=WIFI_MODE_STA;recovery_admitted=false;recovery_owner_pending=true;
        esp32_mquickjs_wifi_recovery_t state={0};bool complete;
        assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_OK);
        for(unsigned step=0;step<point;++step)
            assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
        esp32_mquickjs_wifi_recovery_dispose(&state);
        assert(!temporary && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41);
        assert(wifi_finish_configuration_cleanup()==ESP_ERR_TIMEOUT);
        recovery_owner_pending=false;
        assert(wifi_finish_configuration_cleanup()==ESP_OK && !s_wifi_lifecycle.identity);
    }
    setup_restart();recovery_mode=WIFI_MODE_STA;recovery_admitted=false;recovery_owner_pending=false;
    esp32_mquickjs_wifi_recovery_t state={0};bool complete;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_OK);
    for(unsigned step=0;step<5;++step)
        assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_OK && !complete);
    phase_failure=2;
    assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==-102 && !complete);
    assert(!recovery_admitted && !strcmp(state.execution.stage,"restart-rebuild"));
    unsigned old_rebuild=phase_calls[2];
    assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_ERR_INVALID_STATE);
    assert(phase_calls[2]==old_rebuild);
    phase_failure=0;
    assert(wifi_finish_configuration_cleanup()==ESP_OK && !s_wifi_lifecycle.identity);
    esp32_mquickjs_wifi_recovery_dispose(&state);assert(!temporary);

    setup_restart();recovery_mode=WIFI_MODE_STA;recovery_admitted=false;recovery_owner_pending=false;
    assert(esp32_mquickjs_wifi_recovery_begin(&state,&original,false)==ESP_OK);
    assert(wifi_finish_configuration_cleanup()==ESP_OK);
    /* A different lifecycle now owns the coordinator: stale step cannot touch it. */
    native_token=s_wifi_lifecycle=(esp32_mquickjs_wifi_radio_lifecycle_t){10,42};
    s_wifi_configuration_cleanup=true;
    unsigned before=stop_calls;
    assert(esp32_mquickjs_wifi_recovery_step(&state,&complete)==ESP_ERR_INVALID_STATE);
    assert(s_wifi_lifecycle.identity==42 && stop_calls==before);
    esp32_mquickjs_wifi_recovery_dispose(&state);assert(!temporary);
}
int main(void) {
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
    return 0;
}
