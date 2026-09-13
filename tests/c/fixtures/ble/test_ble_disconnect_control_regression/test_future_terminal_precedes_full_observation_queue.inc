
int main(void) {
    check_future_at_publish=true;
    struct ble_gap_event event={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn={7},.reason=19}};
    dispatch(&event);
    assert(operation.completed && operation.host_code==0 && wakes==1);
    assert(published==1 && s_ble.dropped_connection_events==1 && connection.terminal_pending);
}
