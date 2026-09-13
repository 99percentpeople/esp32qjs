
int main(void) {
    reset_action();size_t size=sizeof(wifi_action_tx_req_t)+1;
    wifi_action_tx_req_t *request=calloc(1,size);assert(request);
    *request=(wifi_action_tx_req_t){.ifx=WIFI_IF_STA,.type=WIFI_OFFCHAN_TX_REQ,.channel=6,
        .wait_time_ms=100,.rx_cb=esp32_mquickjs_wifi_action_receive,.data_len=1};
    esp32_mquickjs_future_driver_state_t state={.submitted=true};
    atomic_init(&state.worker_done,false);atomic_init(&state.cancel_requested,false);
    assert(esp32_mquickjs_wifi_radio_action_send(request,size,&state.token)==ESP_OK);
    wifi_radio_operation_lock();
    assert(esp32_mquickjs_wifi_action_terminated(&s_action.lane,&state.token));
    wifi_radio_operation_unlock();
    assert(action_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && !state.error);
    atomic_store_explicit(&state.worker_done,true,memory_order_release);
    assert(action_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && state.error==ESP_ERR_INVALID_STATE);
    assert(!strcmp(state.stage,"native-terminated") && !state.result.terminal && state.token.identity);
    assert(state.result.physical_termination && !posts && !fence_calls && !quiescent_calls);
    /* Independent native termination remains the cause even if delivered
     * statuses conflicted before the physical teardown was proven. */
    state.error=0;s_action.lane.ambiguous=true;
    assert(action_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && !strcmp(state.stage,"native-terminated"));
    /* Already published submit errors retain their own cause. */
    state.error=73;state.stage="submit";
    assert(action_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && state.error==73 && !strcmp(state.stage,"submit"));
    esp32_mquickjs_wifi_action_lane_t native;
    assert(esp32_mquickjs_wifi_radio_action_retire(&state.token,&native)==0 && !action_owners());
    free(request);return 0;
}
