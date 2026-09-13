
int main(void) {
    s_wifi_state.scan_generation=7;s_wifi_state.scan_in_progress=true;
    s_wifi_state.scan_future_registered=true;s_wifi_state.scan_queue=(void *)1;
    esp32_mquickjs_future_driver_state_t future={.kind=WIFI_FUTURE_SCAN,.started=true,.generation=7};
    assert(wifi_future_cancel(&future)==ESP32_MQUICKJS_CANCELLED);
    assert(future.completed && !s_wifi_state.scan_future_registered);
    assert(s_wifi_state.scan_in_progress); /* Public cancellation is not native completion. */
    assert(stops==1 && clears==0);
    const esp32_mquickjs_wifi_driver_event_t event={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&event);
    assert(!s_wifi_state.scan_in_progress && queued==0);
}
