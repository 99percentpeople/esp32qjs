
int main(void) {
    s_wifi_state.connect_generation=UINT32_MAX;
    esp32_mquickjs_future_driver_state_t state={.kind=WIFI_FUTURE_CONNECT};
    assert(!wifi_future_start(NULL,NULL,1,&state));
    assert(!prepare_connect_calls && !s_wifi_state.connect_future_registered);
    s_wifi_state.connect_generation=8;
    prepare_connect_error=-70;
    assert(!wifi_future_start(NULL,NULL,1,&state));
    assert(prepare_connect_calls==1 && !s_wifi_state.connect_future_registered);
    assert(s_wifi_state.connect_generation==8 && !state.started);
    prepare_connect_error=0;
    assert(wifi_future_start(NULL,NULL,2,&state));
    assert(prepare_connect_calls==2 && s_wifi_state.connect_future_registered);
    assert(s_wifi_state.connect_generation==9 && state.started);
}
