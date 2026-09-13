
static void callback_exit(void) {
    assert(!s_wifi_runtime && !cleanup_calls);
    atomic_store(&s_wifi_state.callbacks_active,0);
}
int main(void) {
    s_wifi_state.initialized=true;
    atomic_store(&s_wifi_state.callbacks_active,1);on_delay=callback_exit;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(ticks==1 && cleanup_calls==1 && !disconnect_calls && !stops);
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(cleanup_calls==1);
}
