

static void reset(void){
 assert(!s_wps_active && !s_wps_handles && !s_wps_workers && !scheduled && !helper_identity && !radio_active);
 alloc_fail=queue_fail=partial_start=capture_ready=hold_close=hold_helper=false;
 start_error=poll_error=finish_error=reserve_error=0;
 assert(!esp32_mquickjs_wifi_wps_ap_open_runtime());
}
