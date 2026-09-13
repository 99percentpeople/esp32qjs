
int main(void) {
    terminate_result=17;
    ble_reject_incoming_connection(7);
    assert(rejected.allocated && rejected.open && rejected.release_on_disconnect);
    assert(s_ble.rejected_connection_error==17);
    /* A closed JS handle must not shield a reused native handle from rejection. */
    existing=(ble_connection_slot_t){.allocated=true,.open=false,.conn_handle=8};
    no_slot=true;
    ble_reject_incoming_connection(8);
    assert(terminated_handle==8 && terminated==2);
    assert(s_ble.rejected_connection_error==17);
    no_slot=false;terminate_result=BLE_HS_ENOTCONN;
    memset(&rejected,0,sizeof(rejected));
    ble_reject_incoming_connection(10);
    assert(!rejected.allocated && !rejected.open);
    assert(rejected.conn_handle==BLE_INVALID_CONN_HANDLE);
}
