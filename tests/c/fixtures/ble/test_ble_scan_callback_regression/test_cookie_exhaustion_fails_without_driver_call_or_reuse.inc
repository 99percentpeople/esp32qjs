
int main(void) {
    s_ble_scan_next_identity=UINTPTR_MAX;
    esp32_mquickjs_future_driver_state_t state={.adapter_generation=1,.connection_generation=1};
    s_ble.scanner.generation=1;
    assert(ble_scan_start(NULL,NULL,1,&state));
    assert((uintptr_t)saved_arg==UINTPTR_MAX);
    ble_release_scanner(&s_ble);
    s_ble.scanner.generation=1;
    assert(!ble_scan_start(NULL,NULL,1,&state));
    assert(driver_calls==1 && s_ble_scan_next_identity==0);
}
