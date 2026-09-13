
int main(void) {
    esp32_mquickjs_future_driver_state_t a={.kind=WIFI_FUTURE_SCAN};
    s_wifi_state.scan_generation=UINT32_MAX;
    assert(!wifi_future_start(NULL,NULL,1,&a));
    assert(wifi_future_cancel(&a)==ESP32_MQUICKJS_CANCELLED);
    assert(!stops && !starts && !clears && s_wifi_state.scan_generation==UINT32_MAX);
    a=(esp32_mquickjs_future_driver_state_t){.kind=WIFI_FUTURE_CONNECT};
    s_wifi_state.connect_generation=UINT32_MAX;
    assert(!wifi_future_start(NULL,NULL,2,&a));
    assert(!s_wifi_state.connect_future_registered);
}
