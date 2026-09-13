
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    assert(ble_connect_finish(NULL,&state)==JS_EXCEPTION);
    assert(!state.transferred);
    ble_connect_destroy(&state);
    assert(terminated==1 && s_ble.connections[0].allocated);
    assert(s_ble.connections[0].release_on_disconnect);
}
