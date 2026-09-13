
static void deliver(void) {
    assert(!s_wifi_runtime && !cleanup_calls);
    if(ticks==2) {
        const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
        wifi_process_driver_event(&done);
    }
    if(ticks==4) {
        const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
        wifi_process_driver_event(&fence);
    }
}
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.status.connected=true;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    on_delay=deliver;
    assert(wifi_finish_runtime_cleanup(true)==ESP_OK);
    assert(ticks==4 && disconnect_calls==1 && cleanup_calls==1);
}
