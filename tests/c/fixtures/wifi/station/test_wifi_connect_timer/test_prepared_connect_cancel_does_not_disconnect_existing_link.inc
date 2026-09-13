
int main(void) {
    s_wifi_state.status.connected=true;
    esp32_mquickjs_future_driver_state_t future={.kind=WIFI_FUTURE_CONNECT};
    assert(wifi_future_cancel(&future)==ESP32_MQUICKJS_CANCELLED);
    assert(disconnects==0 && wakes==0 && s_wifi_state.status.connected);
}
