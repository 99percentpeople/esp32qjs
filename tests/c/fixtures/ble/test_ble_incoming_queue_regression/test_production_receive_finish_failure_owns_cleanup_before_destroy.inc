
int main(void) {
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    ble_advertiser_event_t event={.generation=4,.connection_generation=7};
    esp32_mquickjs_event_queue_t queue={ble_advertiser_event_to_js,ble_advertiser_event_drop,&s_ble.advertiser};
    esp32_mquickjs_future_driver_state_t state={.queue=&queue,.received=true,
        .native_queue_retained=true,.event=&event};
    fail_at=3;
    assert(event_queue_future_finish(NULL,&state)==JS_EXCEPTION);
    assert(state.event_finished && terminated==1 && retired==1 && !roots);
    event_queue_future_destroy(&state);
    assert(terminated==1 && released_storage==2 && released_queue==1);
}
