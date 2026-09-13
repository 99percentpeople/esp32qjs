
int main(void) {
    s_wifi_state.scan_queue=(void *)1;
    esp32_mquickjs_future_driver_state_t a={.kind=WIFI_FUTURE_SCAN},b={.kind=WIFI_FUTURE_SCAN},connect={.kind=WIFI_FUTURE_CONNECT};
    assert(wifi_future_start(NULL,NULL,1,&a));
    assert(wifi_future_cancel(&a)==ESP32_MQUICKJS_CANCELLED);
    assert(!wifi_future_start(NULL,NULL,2,&b));
    assert(!wifi_future_start(NULL,NULL,3,&connect));
    assert(starts==1 && stops==1 && clears==0);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);
    clear_error=-9;
    assert(!wifi_future_start(NULL,NULL,2,&b));
    assert(s_wifi_state.scan_draining && s_wifi_state.scan_cleanup_error==-9 && starts==1);
    clear_error=0;
    assert(wifi_future_start(NULL,NULL,2,&b));
    assert(b.generation!=a.generation && starts==2 && !queued);
    assert(esp32_mquickjs_wifi_cancel_scan(a.generation)==0);
    assert(s_wifi_state.scan_future_registered && s_wifi_state.scan_in_progress);
    wifi_process_driver_event(&done);
    assert(queued==1 && last_generation==(int)b.generation);
}
