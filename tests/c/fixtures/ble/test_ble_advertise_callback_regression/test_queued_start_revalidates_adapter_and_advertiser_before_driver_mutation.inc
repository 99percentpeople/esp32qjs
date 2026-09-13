
int main(void) {
    esp32_mquickjs_future_driver_state_t state={0};
    s_ble.lifecycle=0;
    assert(!ble_advertise_start(NULL,NULL,1,&state));
    s_ble.lifecycle=BLE_LIFECYCLE_ACTIVE;
    s_ble.generation=1;
    assert(!ble_advertise_start(NULL,NULL,1,&state));
    state.adapter_generation=1;s_ble.advertiser.generation=1;
    assert(!ble_advertise_start(NULL,NULL,1,&state));
    assert(driver_calls==0 && s_ble_advertise_next_identity==1);
}
