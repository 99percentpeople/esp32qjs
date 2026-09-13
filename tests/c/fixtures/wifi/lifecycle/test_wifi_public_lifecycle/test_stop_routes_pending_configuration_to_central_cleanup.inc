
int main(void) {
    s_wifi_configuration_cleanup=true;s_wifi_lifecycle.identity=7;
    s_wifi_state.runtime_cleanup_pending=true;configuration_cleanup_error=-8;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==-99);
    assert(configuration_cleanup_calls==1 && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==7);
    assert(!begin_calls && !cleanup_calls && !disconnect_calls && !stops);
    configuration_cleanup_error=0;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==123);
    assert(configuration_cleanup_calls==2 && !s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
    assert(!s_wifi_state.runtime_cleanup_pending);
    assert(!begin_calls && !cleanup_calls && !disconnect_calls && !stops);
}
