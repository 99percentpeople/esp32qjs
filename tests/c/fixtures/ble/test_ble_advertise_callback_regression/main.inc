
int main(void) {
    esp32_mquickjs_future_driver_state_t state={0};
    immediate=IMMEDIATE;
    assert(ble_advertise_start(NULL,NULL,1,&state));
    if(immediate) { assert(!s_ble.advertiser.active);return 0; }
    callback_t old_cb=saved_callback; void *old_arg=saved_arg;
    /* Native stop succeeded after it copied the old cb/arg. */
    s_ble.advertiser.active=false;
    ble_release_advertiser(&s_ble);
    assert(ble_advertise_start(NULL,NULL,2,&state));
    struct ble_gap_event event={.type=BLE_GAP_EVENT_ADV_COMPLETE};
    old_cb(&event,old_arg);
    assert(s_ble.advertiser.active);
}
