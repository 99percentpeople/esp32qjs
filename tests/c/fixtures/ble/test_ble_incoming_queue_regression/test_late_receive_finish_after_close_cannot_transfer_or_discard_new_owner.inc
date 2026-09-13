
int main(void) {
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    ble_advertiser_event_t old={.generation=4,.connection_generation=7};
    ble_release_advertiser(&s_ble);
    assert(ble_advertiser_event_to_js(NULL,&old,&s_ble.advertiser)==JS_EXCEPTION);
    assert(!roots && !calls && terminated==1);
    s_ble.advertiser.generation=5;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=8,.incoming_advertiser_generation=5,.conn_handle=12};
    assert(ble_advertiser_event_to_js(NULL,&old,&s_ble.advertiser)==JS_EXCEPTION);
    assert(!roots && !calls && terminated==1 && s_ble.connections[0].incoming_pending);
}
