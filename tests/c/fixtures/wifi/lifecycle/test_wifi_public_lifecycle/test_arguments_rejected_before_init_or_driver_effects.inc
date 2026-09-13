
int main(void) {
    assert(js_wifi_start(NULL,NULL,2,NULL)==-98);
    assert(js_wifi_stop(NULL,NULL,2,NULL)==-98);
    assert(!init_calls && !begin_calls && !acquire_calls && !cleanup_calls);
}
