
int main(void) {
    create();
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,.conn_handle=12};
    ble_advertiser_event_t event={.generation=4,.connection_generation=6};
    registered_drop(&event,registered_opaque);
    assert(!terminated && s_ble.connections[0].incoming_pending);
    event.connection_generation=7;event.generation=3;
    registered_drop(&event,registered_opaque);
    assert(!terminated && s_ble.connections[0].incoming_pending);
    event.generation=4;terminate_result=17;
    registered_drop(&event,registered_opaque);
    assert(terminated==1 && last_handle==11 && s_ble.rejected_connection_error==17);
    assert(s_ble.connections[0].open && s_ble.connections[0].allocated);
    assert(s_ble.connections[0].release_on_disconnect && !s_ble.connections[0].incoming_pending);
    registered_drop(&event,registered_opaque);
    assert(terminated==1 && s_ble.connections[1].open && !s_ble.connections[1].release_on_disconnect);
}
