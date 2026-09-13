
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.status.connected=true;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(wifi_finish_runtime_cleanup(true)==ESP_ERR_INVALID_STATE);
    assert(ticks==1000 && disconnect_calls==1 && !cleanup_calls);
    assert(s_wifi_state.initialized && s_wifi_state.runtime_cleanup_pending);
    assert(!strcmp(s_wifi_state.cleanup_stage,"connection-drain"));
}
