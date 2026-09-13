
int main(void) {
    stop_capture_error=true;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==JS_EXCEPTION && !wait_begins && !begin_calls);
    stop_capture_error=false;wait_begin_error=-77;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==JS_EXCEPTION && !wait_ends && !begin_calls);
    wait_begin_error=0;begin_error=-7;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==JS_EXCEPTION && wait_ends==1 && !wait_scope);
    assert(!cleanup_calls && !s_wifi_lifecycle.identity);
    begin_error=0;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==123 && wait_ends==2 && !wait_scope);
}
