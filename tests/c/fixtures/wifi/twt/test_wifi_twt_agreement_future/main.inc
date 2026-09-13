
static esp32_mquickjs_future_driver_state_t *capture(void) {
    esp32_mquickjs_future_driver_state_t *state=calloc(1,sizeof(*state));assert(state);
    state->handle=calloc(1,sizeof(*state->handle));assert(state->handle);
    state->options=(esp32_mquickjs_wifi_itwt_options_t){.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=6000};
    atomic_init(&state->worker_done,false);atomic_init(&state->cancel_requested,false);return state;
}
int main(void) {
    individual_reset();
    esp32_mquickjs_future_driver_state_t *state=capture();
    assert(agreement_start(NULL,NULL,0,state) && worker);
    assert(agreement_cancel(state)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !individual_submits);
    agreement_destroy(state);assert(!individual_owners());
    state=capture();assert(agreement_start(NULL,NULL,0,state));work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING && individual_owners()==1);
    individual_results[0].flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN;
    individual_results[0].event=(wifi_event_sta_itwt_setup_t){.config=state->options.config,.status=1};
    individual_results[0].event.config.setup_cmd=TWT_ACCEPT;
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !state->error);
    state->result.native.flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED;
    individual_results[0].flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED;
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && state->error==ESP_ERR_INVALID_STATE);
    /* Public conversion failure/cancel leaves ownership in the Radio registry. */
    retirement_error=ESP_ERR_NOT_FINISHED;agreement_destroy(state);assert(worker && individual_owners()==1);work();
    assert(individual_owners()==1 && !worker);
    assert(!esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    now_us+=100000;retirement_error=0;
    assert(esp32_mquickjs_wifi_twt_agreement_service() && worker);work();
    assert(!individual_owners() && esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    esp32_mquickjs_wifi_twt_token_t a={0},b={0};state=capture();
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&state->options,&a));
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&state->options,&b));
    heap_caps_free(state->handle);state->handle=NULL;state->close=true;state->token=a;
    assert(agreement_start(NULL,NULL,0,state));assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(!esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    now_us+=100000;(void)esp32_mquickjs_wifi_twt_agreement_service();assert(worker);work();
    assert(!individual_owners() && agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY);
    agreement_destroy(state);assert(!worker && !locks && !critical);free(s_twt_individual);return 0;
}
