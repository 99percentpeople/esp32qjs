
int main(void) {
    esp32_mquickjs_wifi_driver_event_t e = {.kind=2};
    assert(wifi_publish_driver_event_from_callback(&e));
    assert(processed==2);
    e.kind=3;
    assert(wifi_publish_driver_event_from_callback(&e));
    assert(processed==3);
    e.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT;e.generation=7;
    assert(wifi_publish_driver_event_from_callback(&e));
    assert(atomic_load(&s_wifi_timeout_pending)==7);
}
