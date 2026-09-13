
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    terminate_result=17;
    ble_connect_destroy(&state);
    assert(terminated==1 && released==1);
    assert(s_ble.connections[0].open && s_ble.connections[0].allocated);
    assert(s_ble.connections[0].release_on_disconnect);
    assert(!esp32_mquickjs_wireless_native_operation_is_quiescent(&s_ble.connections[0].connect_operation));
}
