
int main(void) {
    esp32_mquickjs_wifi_radio_lifecycle_t token={1,7},stale={2,7};s_radio.lifecycle=token;
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(NULL)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&stale)==ESP_ERR_INVALID_STATE && !initializes && !depth);
    s_radio.leases[1].identity=9;
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==ESP_ERR_INVALID_STATE && !initializes);
    s_radio.leases[1].identity=0;s_radio.stop_required=true;
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==ESP_ERR_INVALID_STATE && !initializes);
    s_radio.stop_required=false;s_radio.wake_locks=1;
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==ESP_ERR_INVALID_STATE && !initializes);
    s_radio.wake_locks=0;s_radio.operation.identity=3;
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==ESP_ERR_INVALID_STATE && !initializes);
    s_radio.operation.identity=0;s_radio.fault_stage="init";
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==ESP_ERR_INVALID_STATE && !initializes);
    s_radio.fault_stage=NULL;init_error=-77;
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==-77 && initializes==1 && !depth && token.identity==7);
    init_error=0;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==ESP_OK && initializes==2 && token.identity==7);
    return 0;
}
