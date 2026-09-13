
int main(void) {
    cleanup_failure=-6;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==-99);
    assert(begin_calls==1 && cleanup_calls==1 && s_wifi_lifecycle.identity);
    assert(js_wifi_start(NULL,NULL,0,NULL)==-99 && !acquire_calls);
    cleanup_failure=0;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==123);
    assert(begin_calls==1 && cleanup_calls==2);
}
