
static esp32_mquickjs_future_driver_state_t *capture_broadcast(unsigned slot) {
    esp32_mquickjs_future_driver_state_t *state=calloc(1,sizeof(*state));assert(state);
    state->handle=calloc(1,sizeof(*state->handle));assert(state->handle);
    state->broadcast=true;
    state->broadcast_options=(esp32_mquickjs_wifi_btwt_options_t){.config={.setup_cmd=TWT_REQUEST,.btwt_id=slot,
        .timeout_time_ms=5000},.timeout_ms=6000};
    atomic_init(&state->worker_done,false);atomic_init(&state->cancel_requested,false);return state;
}
int main(void) {
    individual_reset();
    esp32_mquickjs_future_driver_state_t *state=capture_broadcast(1);
    assert(agreement_timeout(state)==6000 && agreement_start(NULL,NULL,0,state));
    assert(agreement_cancel(state)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !broadcast_submits);
    agreement_destroy(state);assert(!individual_owners());
    state=capture_broadcast(1);assert(agreement_start(NULL,NULL,0,state));work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING && individual_owners()==1);
    broadcast_results[1].seen=true;broadcast_results[1].complete=true;broadcast_results[1].tx_busy=true;
    broadcast_results[1].event=(wifi_event_sta_btwt_setup_t){.status=BTWT_SETUP_SUCCESS,.setup_cmd=TWT_ACCEPT,
        .btwt_id=1,.min_wake_dura=1,.wake_invl_mant=100};
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
    broadcast_results[1].tx_busy=false;broadcast_results[1].observation_error=99;
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !state->error);
    /* Failed object construction still owns native storage after public completion. */
    broadcast_retire_error=ESP_ERR_NOT_FINISHED;agreement_destroy(state);assert(worker);work();
    assert(individual_owners()==1 && !esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    broadcast_retire_error=0;now_us+=100000;
    assert(esp32_mquickjs_wifi_twt_agreement_service());work();
    assert(!individual_owners() && esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    state=capture_broadcast(2);assert(agreement_start(NULL,NULL,0,state));work();
    broadcast_results[2].complete=true;broadcast_results[2].native_error=81;
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && state->error==81);
    agreement_destroy(state);now_us+=100000;(void)esp32_mquickjs_wifi_twt_agreement_service();work();
    state=capture_broadcast(3);assert(agreement_start(NULL,NULL,0,state));work();
    state->close=true;state->submitted=false;
    assert(agreement_start(NULL,NULL,0,state));
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
    now_us+=100000;assert(esp32_mquickjs_wifi_twt_agreement_service());work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !state->error);
    agreement_destroy(state);
    assert(!locks && !critical && !individual_owners());free(s_twt_broadcast);return 0;
}
