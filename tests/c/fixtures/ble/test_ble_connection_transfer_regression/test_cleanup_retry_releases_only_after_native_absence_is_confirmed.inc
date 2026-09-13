
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    ble_connection_slot_t *slot=&s_ble.connections[0];
    *slot=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    terminate_result=17;ble_connect_destroy(&state);
    assert(slot->allocated && slot->connect_operation && s_ble.rejected_connection_error==17);
    terminate_result=0;ble_terminate_unclaimed_connection(slot, slot->generation);
    assert(slot->allocated && slot->open && slot->connect_operation);
    terminate_result=BLE_HS_ENOTCONN;ble_terminate_unclaimed_connection(slot, slot->generation);
    assert(!slot->allocated && !slot->open && !slot->connect_operation);
    assert(!slot->release_on_disconnect && slot->conn_handle==BLE_INVALID_CONN_HANDLE);
    assert(terminated==3 && s_ble.rejected_connection_error==17);
}
