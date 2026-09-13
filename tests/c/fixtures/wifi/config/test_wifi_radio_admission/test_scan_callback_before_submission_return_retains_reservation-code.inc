starts++;
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);
    s_wifi_state.scan_results_pending=false;
    wifi_release_radio_operation();
    assert(!operation_releases && s_wifi_state.radio_operation.identity);
    return start_error;