
static void wifi_release_radio_operation(void);
static int esp_wifi_connect(void) {
    assert(!lock_depth && !counter_lock_depth);connects++;
    const esp32_mquickjs_wifi_driver_event_t ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
    wifi_process_driver_event(&ip);wifi_release_radio_operation();
    assert(operation_releases==1 && s_wifi_state.radio_operation.identity);
    return 0;
}
