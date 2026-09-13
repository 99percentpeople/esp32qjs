
int main(void) {
    s_ble.lifecycle=BLE_LIFECYCLE_FAILED;connection.release_on_disconnect=true;
    struct ble_gap_event event={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn={7}}};
    dispatch(&event);
    assert(!connection.open && !connection.allocated && !connection.connect_operation);
    assert(!published && connection.conn_handle==BLE_INVALID_CONN_HANDLE);
}
