
int main(void) {
    s_ble.advertiser.generation=4;
    /* Slot 0's event has already left the queue for a receive Future. */
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,
        .generation=8,.incoming_advertiser_generation=4,.conn_handle=12};
    s_ble.connections[2]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=9,.incoming_advertiser_generation=5,.conn_handle=13};
    ble_advertiser_event_t event={.generation=4,.connection_generation=7};
    ble_release_advertiser(&s_ble);
    assert(terminated==1 && last_handle==11 && quiesced==1);
    ble_advertiser_event_drop(&event,&s_ble.advertiser);
    assert(terminated==1 && s_ble.connections[1].open && s_ble.connections[2].incoming_pending);
}
