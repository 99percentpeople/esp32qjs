
int main(void) {
    setup();timeout_capture_error=true;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==JS_EXCEPTION);
    assert(!wait_begins && !partial_admissions && s_ap_lease.acquired);
    timeout_capture_error=false;wait_begin_error=-67;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==JS_EXCEPTION);
    assert(!wait_ends && !partial_admissions && s_ap_lease.acquired);
    wait_begin_error=0;timeout_budget=37;partial_fail=1;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==JS_EXCEPTION);
    assert(wait_ends==1 && !wait_scope && s_wifi_ap_stop_cleanup && partial_mutations==1);
    unsigned station=s_wifi_state.radio_lease.identity,app=s_wifi_application.identity;
    partial_fail=0;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123);
    assert(wait_ends==2 && !wait_scope && partial_mutations==1 && !s_wifi_ap_stop_cleanup);
    assert(s_wifi_state.radio_lease.identity==station && s_wifi_application.identity==app);
}
