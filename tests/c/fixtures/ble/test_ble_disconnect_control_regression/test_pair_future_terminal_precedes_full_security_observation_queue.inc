
int main(void) {
    check_future_at_publish=true;operation.operation=BLE_OP_PAIR;
    struct ble_gap_event event={.type=BLE_GAP_EVENT_ENC_CHANGE,.enc_change={.conn_handle=7,.status=0}};
    dispatch(&event);
    assert(operation.completed && operation.host_code==0 && wakes==1 && native_completed==1);
    assert(published==1 && s_ble.dropped_connection_events==1);
}
