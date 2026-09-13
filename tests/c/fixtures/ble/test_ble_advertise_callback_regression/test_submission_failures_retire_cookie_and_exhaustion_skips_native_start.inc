
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.scan_response_length=1};
    for(int stage=0;stage<3;stage++) {
        data_result=stage==0 ? 17 : 0;
        rsp_result=stage==1 ? 17 : 0;
        driver_result=stage==2 ? 17 : 0;
        assert(!ble_advertise_start(NULL,NULL,1,&state));
        assert(!s_ble.advertiser.active && s_ble_advertise_callback_identity==0);
    }
    driver_result=0;s_ble_advertise_next_identity=UINTPTR_MAX;
    assert(ble_advertise_start(NULL,NULL,1,&state));
    assert((uintptr_t)saved_arg==UINTPTR_MAX);
    ble_release_advertiser(&s_ble);
    int previous=driver_calls;
    assert(!ble_advertise_start(NULL,NULL,1,&state));
    assert(driver_calls==previous && s_ble_advertise_next_identity==0);
}
