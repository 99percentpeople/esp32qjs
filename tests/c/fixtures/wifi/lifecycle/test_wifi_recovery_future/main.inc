
static void check_kind(esp32_mquickjs_wifi_recovery_kind_t kind) {
    original.kind=kind;clock_us=0;wait_begins=wait_ends=0;
    setup_restart();recovery_mode=WIFI_MODE_STA;recovery_owner_pending=true;
    esp32_mquickjs_future_driver_state_t state={.operation={9,51,kind},.timeout_ms=1000};
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && !begin_calls);
    assert(recovery_start(NULL,NULL,0,&state));
    clock_us=1234;
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && state.recovery.execution.admitted);
    assert(last_wait==999 && wait_begins==wait_ends);
    for(unsigned i=0;i<4;++i)assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && !phase_calls[2]);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && !phase_calls[2]);
    recovery_owner_pending=false;
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && state.complete && !state.error);
    assert(phase_calls[2]==1 && wait_begins==wait_ends);
    esp32_mquickjs_wifi_recovery_dispose(&state.recovery);

    setup_restart();recovery_admitted=false;recovery_owner_pending=true;
    state=(esp32_mquickjs_future_driver_state_t){.operation={9,51,kind},.timeout_ms=1000};
    assert(recovery_cancel(&state)==ESP32_MQUICKJS_CANCELLED);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && !begin_calls);
    state.cancelled=false;assert(recovery_start(NULL,NULL,0,&state));
    wait_error=77;assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && state.error==77 && !begin_calls);
    wait_error=state.error=0;
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && begin_calls==1);
    unsigned before=stop_calls;
    clock_us=state.deadline_us;
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && stop_calls==before);
    assert(recovery_cancel(&state)==ESP32_MQUICKJS_CANCELLED);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && !phase_calls[1]);
    esp32_mquickjs_wifi_recovery_dispose(&state.recovery);
    assert(s_wifi_configuration_cleanup && !temporary);
    assert(wifi_finish_configuration_cleanup()==ESP_ERR_TIMEOUT);
    recovery_owner_pending=false;
    assert(wifi_finish_configuration_cleanup()==ESP_OK);
}
int main(void) {
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
    return 0;
}
