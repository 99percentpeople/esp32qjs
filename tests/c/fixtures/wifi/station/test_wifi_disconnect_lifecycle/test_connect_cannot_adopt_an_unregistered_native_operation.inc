
int main(void) {
    s_wifi_state.connect_generation=7;s_wifi_state.connect_in_progress=true;
    esp32_mquickjs_future_driver_state_t connect={.kind=WIFI_FUTURE_CONNECT};
    assert(!wifi_future_start(NULL,NULL,8,&connect));
    assert(!s_wifi_state.connect_future_registered && s_wifi_state.connect_generation==7);
}
