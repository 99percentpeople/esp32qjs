
int main(void) {
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    ble_advertiser_event_t event={.generation=4,.connection_generation=7};
    ble_advertiser_event_drop(&event,NULL);
    assert(!terminated && !s_ble.connections[0].allocated);
    s_ble.connections[0].open=true;s_ble.connections[0].allocated=true;
    s_ble.connections[0].incoming_pending=true;s_ble.host_started=false;
    ble_release_advertiser(&s_ble);
    assert(!terminated && !s_ble.connections[0].allocated);
}
