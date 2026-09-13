
int main(void) {
    admission_error=ESP_ERR_INVALID_STATE;
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    assert(!wifi_future_start(NULL,NULL,1,&scan));
    assert(starts==0 && !s_wifi_state.scan_in_progress && !s_wifi_state.scan_future_registered);
}
