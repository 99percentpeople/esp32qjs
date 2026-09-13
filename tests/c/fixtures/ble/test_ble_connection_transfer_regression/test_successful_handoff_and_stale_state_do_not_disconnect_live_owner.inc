
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    result=42;
    assert(ble_connect_finish(NULL,&state)==42 && state.transferred);
    ble_connect_destroy(&state);
    assert(terminated==0);
    state.transferred=false;state.connection_generation=0;
    ble_connect_destroy(&state);
    assert(terminated==0 && s_ble.connections[0].open);
}
