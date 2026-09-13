int main(void) {
    wifi_scan_config_t config={0};
    s_wifi_state.scan_generation=7;s_wifi_state.scan_future_registered=true;
    s_wifi_state.scan_queue=(void *)1;
    assert(esp32_mquickjs_wifi_start_scan(&config,7)==ESP_OK);
    assert(esp32_mquickjs_wifi_stop_scan_for_results(6)==ESP_ERR_INVALID_STATE);
    assert(stops==0);
    assert(esp32_mquickjs_wifi_stop_scan_for_results(7)==ESP_OK);
    assert(stops==1 && clears==0 && s_wifi_state.scan_future_registered);
    assert(s_wifi_state.scan_in_progress && s_wifi_state.radio_operation.identity);
    /* SDK stop acknowledged; get_ap_records has consumed the stopped list,
     * but the event loop has not yet delivered SCAN_DONE. */
    esp32_mquickjs_wifi_scan_results_consumed();
    assert(esp32_mquickjs_wifi_cancel_scan(7)==ESP_OK);
    assert(stops==1 && clears==0 && s_wifi_state.scan_draining);
    assert(esp32_mquickjs_wifi_start_scan(&config,7)==ESP_ERR_INVALID_STATE);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE,.status=1};
    wifi_process_driver_event(&done);
    assert(!s_wifi_state.scan_results_pending);
    assert(esp32_mquickjs_wifi_drain_scan()==ESP_OK);
    assert(clears==0 && !s_wifi_state.radio_operation.identity && !s_wifi_state.scan_draining);
    s_wifi_state.scan_generation=8;s_wifi_state.scan_future_registered=true;
    assert(esp32_mquickjs_wifi_start_scan(&config,8)==ESP_OK);
    complete_in_stop=true;
    assert(esp32_mquickjs_wifi_stop_scan_for_results(8)==ESP_OK);
    assert(clears==0 && s_wifi_state.scan_results_pending && !s_wifi_state.scan_in_progress);
    esp32_mquickjs_wifi_scan_results_consumed();
    assert(esp32_mquickjs_wifi_cancel_scan(8)==ESP_OK);
    assert(clears==0 && !s_wifi_state.scan_results_pending);
    s_wifi_state.scan_generation=9;s_wifi_state.scan_future_registered=true;
    complete_in_stop=false;
    assert(esp32_mquickjs_wifi_start_scan(&config,9)==ESP_OK);
    assert(esp32_mquickjs_wifi_stop_scan_for_results(9)==ESP_OK);
    esp32_mquickjs_wifi_scan_results_consumed();
    /* Event after the early read but before Future detachment: no AP list
     * remains to drain, but the Radio reservation must still be returned. */
    wifi_process_driver_event(&done);
    assert(esp32_mquickjs_wifi_cancel_scan(9)==ESP_OK);
    assert(!s_wifi_state.radio_operation.identity && clears==0);
    return 0;
}
