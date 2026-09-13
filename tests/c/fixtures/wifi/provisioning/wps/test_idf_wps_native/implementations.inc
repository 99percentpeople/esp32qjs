
static int wifi_wps_enable_internal(void *ctx,void *data) {
    assert(ctx==s_wps_native && data);enable_calls++;
    if(!enable_error) {
        gWpsSm=&sample;
        assert(esp32qjs_wps_native_pin("12345670",8));
    }
    return enable_error;
}
static int wifi_station_wps_start(void *data,void *user) {
    assert(data==s_wps_native && !user);start_calls++;
    esp32qjs_wps_register_timeout(120,0,wifi_station_wps_timeout);
    return start_error;
}
/* Fixture disposal separates cases; this is not a production release path or
 * evidence of scan/TX retirement. Production intentionally exposes no release. */
static void fixture_dispose(void) {
    assert(s_wps_native==allocation && allocations==1);
    forced_memzero(allocation,allocation_size);free(allocation);allocation=NULL;
    s_wps_native=NULL;atomic_store(&s_wps_native_held,false);allocations=0;gWpsSm=NULL;
    enable_error=start_error=disconnect_error=type_error=disable_error=timer_error=0;
    enable_calls=start_calls=disconnect_calls=type_calls=disable_calls=posts=0;
    cleanup_fail_stage=-1;deinit_calls=0;deinit_error=0;cancel_count=0;fail_cancel_at=UINT32_MAX;
    memset(cleanup_calls,0,sizeof(cleanup_calls));
    memset(scan_storage,0,sizeof(scan_storage));
    native_wps_status=associated_error=0;memset(associated_bssid,0,sizeof(associated_bssid));
    memset(&sample,0,sizeof(sample));
}
