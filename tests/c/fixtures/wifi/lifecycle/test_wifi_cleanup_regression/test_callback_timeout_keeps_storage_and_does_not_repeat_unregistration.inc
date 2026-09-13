
int main(void) {
    s_wifi_state.lock=s_wifi_state.event_group=s_wifi_state.scan_queue=s_wifi_state.connect_queue=s_wifi_state.driver_event_queue=(void *)1;
    s_wifi_state.connect_timeout_timer=s_wifi_state.wifi_start_event_instance=s_wifi_state.sta_netif=(void *)1;
    s_wifi_state.radio_lease=1;s_wifi_lifecycle.identity=3;s_wifi_state.runtime_cleanup_pending=true;
    atomic_store(&s_wifi_state.callbacks_active,1);
    assert(wifi_cleanup_failed_init()==ESP_ERR_TIMEOUT && !remaining);
    assert(!strcmp(s_wifi_state.cleanup_stage,"callbacks-drain") && s_wifi_state.lock && s_wifi_state.radio_lease);
    assert(s_wifi_lifecycle.identity==3 && !freed && !released && !calls[8]);
    assert(calls[1]==1 && calls[2]==1 && calls[7]==1 && !s_wifi_state.wifi_start_event_instance);
    remaining=10;atomic_store(&s_wifi_state.callbacks_active,0);
    assert(wifi_cleanup_failed_init()==ESP_OK && !s_wifi_lifecycle.identity);
    assert(freed==2 && released==1 && calls[1]==1 && calls[2]==1 && calls[7]==1);
}
