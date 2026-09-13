
static int delivered;
static void deliver_native_events(void) {
    assert(wait_depth && s_wifi_lifecycle.identity==7);
    delivered++;
    if(delivered==1) {
        const esp32_mquickjs_wifi_driver_event_t stale_ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
        wifi_process_driver_event(&stale_ip);assert(!s_wifi_state.status.connected);
        const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
        wifi_process_driver_event(&down);
    } else {
        const esp32_mquickjs_wifi_driver_event_t marker={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,
            .generation=delivered==2?fence_epoch-1:fence_epoch};
        wifi_process_driver_event(&marker);
        if(delivered==2)assert(s_wifi_state.connect_draining);
    }
}
static void deliver_marker(void) {
    const esp32_mquickjs_wifi_driver_event_t marker={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&marker);
}
static void reset(void) {
    assert(!wait_depth && !lock_depth);
    memset(&s_wifi_state,0,sizeof(s_wifi_state));s_wifi_state.sta_netif=(void *)1;
    s_wifi_state.status.connected=true;s_wifi_state.connect_generation=5;
    s_wifi_configuration_cleanup=true;s_wifi_configuration_disconnect_pending=true;s_wifi_lifecycle.identity=7;
    on_delay=NULL;disconnect_calls=disconnect_error=posts=post_error=ticks=delivered=0;
    cooperate_ok=true;complete_disconnect_in_call=false;
}
int main(void) {
    assert(wifi_finish_configuration_disconnect()==ESP_OK && !disconnect_calls && !wait_begins);
    s_wifi_configuration_disconnect_pending=true;
    assert(wifi_finish_configuration_disconnect()==ESP_ERR_INVALID_STATE && !disconnect_calls);
    reset();on_delay=deliver_native_events;
    assert(wifi_finish_configuration_disconnect()==ESP_OK && disconnect_calls==1 && delivered==3);
    assert(!s_wifi_configuration_disconnect_pending && !s_wifi_state.connect_draining && s_wifi_lifecycle.identity==7);
    assert(!s_wifi_state.connect_future_registered && wait_begins==wait_ends);

    reset();assert(wifi_finish_configuration_disconnect()==ESP_ERR_TIMEOUT);
    assert(ticks==1000 && disconnect_calls==1 && s_wifi_configuration_disconnect_pending && s_wifi_lifecycle.identity==7);
    on_delay=deliver_native_events;
    assert(wifi_finish_configuration_disconnect()==ESP_OK && disconnect_calls==1 && !s_wifi_configuration_disconnect_pending);

    reset();complete_disconnect_in_call=true;post_error=-9;
    assert(wifi_finish_configuration_disconnect()==-9 && disconnect_calls==1 && s_wifi_configuration_disconnect_pending);
    assert(s_wifi_state.disconnect_seen && !s_wifi_state.disconnect_fence_posted);
    int previous_posts=posts;post_error=0;on_delay=deliver_marker;
    assert(wifi_finish_configuration_disconnect()==ESP_OK && disconnect_calls==1 && posts==previous_posts+1);

    reset();disconnect_error=-8;
    assert(wifi_finish_configuration_disconnect()==-8 && disconnect_calls==1 && !s_wifi_state.disconnect_submitted);
    disconnect_error=0;on_delay=deliver_native_events;
    assert(wifi_finish_configuration_disconnect()==ESP_OK && disconnect_calls==2);

    reset();cooperate_ok=false;
    assert(wifi_finish_configuration_disconnect()==ESP_ERR_INVALID_STATE && disconnect_calls==1 && s_wifi_configuration_disconnect_pending);
    cooperate_ok=true;on_delay=deliver_native_events;
    assert(wifi_finish_configuration_disconnect()==ESP_OK && disconnect_calls==1);
    assert(wait_begins==wait_ends && !wait_depth && !lock_depth);
    return 0;
}
