
int main(void) {
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    assert(wifi_future_start(NULL,NULL,1,&scan));
    assert(admissions==1 && s_wifi_state.radio_operation.identity);
    assert(wifi_future_cancel(&scan)==ESP32_MQUICKJS_CANCELLED);
    assert(!operation_releases && s_wifi_state.radio_operation.identity);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);clear_error=-9;
    assert(esp32_mquickjs_wifi_drain_scan()==-9 && !operation_releases);
    clear_error=0;assert(esp32_mquickjs_wifi_drain_scan()==0);
    assert(operation_releases==1 && !s_wifi_state.radio_operation.identity);
    assert(esp32_mquickjs_wifi_drain_scan()==0 && operation_releases==1);
}
