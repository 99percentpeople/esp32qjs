
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.connections[0]=(ble_connection_slot_t){.open=false,.allocated=true,.generation=1,.conn_handle=7};
    assert(ble_connection_close_finish(NULL,&state)==JS_TRUE);
    assert(!callback_rebinds && !s_ble.connections[0].allocated);
}
