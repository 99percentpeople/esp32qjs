
int main(void) {
    s_wifi_state.connect_in_progress=true;complete_disconnect_in_call=true;
    assert(wifi_request_disconnect()==ESP_OK);
    assert(disconnect_calls==1 && posts==1);
    assert(!s_wifi_state.connect_draining && (event_bits & WIFI_LINK_DRAINED_BIT));
}
