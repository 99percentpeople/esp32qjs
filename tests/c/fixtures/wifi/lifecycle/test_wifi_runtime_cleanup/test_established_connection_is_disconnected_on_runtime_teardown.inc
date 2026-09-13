
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.started=true;
    s_wifi_state.status.connected=true;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(disconnect_calls==1 && s_wifi_state.connect_draining && !s_wifi_runtime);
}
