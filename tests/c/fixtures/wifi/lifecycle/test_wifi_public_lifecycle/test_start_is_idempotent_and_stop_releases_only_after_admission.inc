
int main(void) {
    assert(js_wifi_start(NULL,NULL,0,NULL)==123);
    assert(js_wifi_start(NULL,NULL,0,NULL)==123);
    assert(acquire_calls==1 && s_wifi_application.acquired);
    begin_error=-7;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==-99);
    assert(!cleanup_calls && s_wifi_application.acquired && !s_wifi_lifecycle.identity);
    begin_error=0;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==123);
    assert(cleanup_calls==1 && !s_wifi_application.acquired && !disconnect_calls && !stops);
}
