
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_in_progress=true;
    s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    atomic_store(&s_wifi_timeout_armed_generation,7);
    wifi_connect_timeout_cb(NULL);
    assert(atomic_load(&s_wifi_timeout_pending)==7 && !disconnects);
    wifi_driver_event_poller(NULL,NULL,NULL);
    assert(!disconnects && s_wifi_state.connect_in_progress);
    atomic_store(&s_wifi_timeout_armed_generation,8);
    wifi_connect_timeout_cb(NULL);
    assert(atomic_load(&s_wifi_timeout_pending)==8 && !disconnects);
    wifi_driver_event_poller(NULL,NULL,NULL);
    assert(disconnects==1 && !s_wifi_state.connect_in_progress);
    atomic_store(&s_wifi_timeout_armed_generation,0);
    wifi_connect_timeout_cb(NULL);
    assert(!atomic_load(&s_wifi_timeout_pending));
}
