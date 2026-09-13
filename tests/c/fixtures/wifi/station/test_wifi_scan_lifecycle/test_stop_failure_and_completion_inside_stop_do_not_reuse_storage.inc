
int main(void) {
    esp32_mquickjs_future_driver_state_t a={.kind=WIFI_FUTURE_SCAN},b={.kind=WIFI_FUTURE_SCAN};
    assert(wifi_future_start(NULL,NULL,1,&a));
    stop_error=-8;
    assert(wifi_future_cancel(&a)==ESP32_MQUICKJS_CANCELLED);
    assert(s_wifi_state.scan_in_progress && s_wifi_state.scan_draining && s_wifi_state.scan_cleanup_error==-8);
    assert(!wifi_future_start(NULL,NULL,2,&b));
    assert(stops==1); /* Admission must not resubmit a failed stop. */
    stop_error=0;complete_in_stop=true;
    assert(esp32_mquickjs_wifi_cancel_scan(a.generation)==0); /* explicit cleanup retry */
    assert(stops==2 && clears==1 && !s_wifi_state.scan_draining);
    assert(wifi_future_start(NULL,NULL,2,&b));
}
