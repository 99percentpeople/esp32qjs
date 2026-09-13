
int main(void) {
    esp32_mquickjs_future_driver_state_t *a=calloc(1,sizeof(*a));
    a->kind=WIFI_FUTURE_SCAN;
    start_error=-7;
    assert(!wifi_future_start(NULL,NULL,1,a));
    assert(!s_wifi_state.scan_in_progress && !s_wifi_state.scan_future_registered);
    start_error=0;
    assert(wifi_future_start(NULL,NULL,2,a));
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);a->completed=true;
    wifi_future_destroy(a);
    assert(clears==1 && stops==0 && !s_wifi_state.scan_results_pending && !s_wifi_state.scan_future_registered);
}
