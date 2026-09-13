
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.adapter_generation=1,.connection_generation=1};
    s_ble.scanner.generation=1;
    driver_result=BLE_HS_EBUSY;
    assert(!ble_scan_start(NULL,NULL,1,&state));
    void *old_arg=saved_arg;
    assert(!s_ble.scanner.active && s_ble_scan_callback_identity==0);
    driver_result=0;
    assert(ble_scan_start(NULL,NULL,1,&state));
    assert(saved_arg!=old_arg);
    struct ble_gap_event event={BLE_GAP_EVENT_DISC_COMPLETE};
    saved_callback(&event,old_arg);
    assert(s_ble.scanner.active);
}
