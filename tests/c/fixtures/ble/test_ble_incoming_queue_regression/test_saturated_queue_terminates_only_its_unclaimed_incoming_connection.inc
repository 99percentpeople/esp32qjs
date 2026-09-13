
int main(void) {
    s_ble.advertiser.generation=4;s_ble.advertiser.active=true;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=7,.conn_handle=11};
    s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,.conn_handle=12};
    queue_full=true;terminate_result=17;
    ble_publish_incoming_connection(&s_ble.connections[0],0);
    assert(!s_ble.advertiser.active && s_ble.advertiser.stop_reason==BLE_STOP_CONNECTED);
    assert(s_ble.advertiser.incoming==1 && s_ble.advertiser.dropped==1);
    assert(terminated==1 && last_handle==11 && s_ble.rejected_connection_error==17);
    assert(s_ble.connections[0].allocated && s_ble.connections[0].release_on_disconnect);
    assert(!s_ble.connections[0].incoming_pending && s_ble.connections[1].open);
    /* A successfully enqueued connection remains queue-owned until conversion. */
    queue_full=false;ble_publish_incoming_connection(&s_ble.connections[1],1);
    assert(s_ble.connections[1].incoming_pending && terminated==1);
    assert(queued.connection_index==1 && queued.connection_generation==0 && queued.generation==4);
    assert(s_ble.advertiser.incoming==2 && s_ble.advertiser.dropped==1);
}
