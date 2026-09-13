
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.lifecycle=1;s_ble.runtime=(void *)1;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,.conn_handle=8};
    terminate_result=17;ble_connect_destroy(&state);
    assert(!esp32_mquickjs_deinit_ble_runtime(NULL));
    assert(terminated==2 && !cleaned && !freed && !reset && s_ble.runtime);
    assert(s_ble.lifecycle==BLE_LIFECYCLE_FAILED);
    terminate_result=BLE_HS_ENOTCONN;cleanup_result=18;
    assert(!esp32_mquickjs_deinit_ble_runtime(NULL));
    assert(terminated==3 && cleaned==1 && !freed && !reset && s_ble.runtime);
    assert(s_ble.connections[1].open && !s_ble.connections[1].release_on_disconnect);
    cleanup_result=0;
    assert(esp32_mquickjs_deinit_ble_runtime(NULL));
    assert(terminated==3 && cleaned==2 && freed==1 && reset==1 && !s_ble.runtime);
    assert(s_ble.lifecycle==BLE_LIFECYCLE_CLOSED);
}
