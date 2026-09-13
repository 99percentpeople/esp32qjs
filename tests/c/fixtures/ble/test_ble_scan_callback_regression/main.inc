
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.adapter_generation=1,.connection_generation=1};
    s_ble.scanner.generation=1;
    s_ble.scanner.payloads=(void *)1;
    immediate=IMMEDIATE;
    assert(ble_scan_start(NULL,NULL,1,&state));
    if (immediate) { assert(!s_ble.scanner.active); return 0; }
    callback_t old_callback=saved_callback;
    void *old_arg=saved_arg;
    /* SDK has already copied cb/arg, but has not entered it when cancel succeeds. */
    s_ble.scanner.active=false;
    ble_release_scanner(&s_ble);
    assert(freed==1);
    s_ble.scanner.generation=2;
    state.connection_generation=2;
    assert(ble_scan_start(NULL,NULL,2,&state));
    struct ble_gap_event report={BLE_GAP_EVENT_DISC};
    old_callback(&report,old_arg);
    assert(delivered==0);
    struct ble_gap_event complete={BLE_GAP_EVENT_DISC_COMPLETE};
    old_callback(&complete,old_arg);
    assert(s_ble.scanner.active);
    saved_callback(&report,saved_arg);
    assert(delivered==1);
    saved_callback(&complete,saved_arg);
    assert(!s_ble.scanner.active);
}
