
int main(void) {
    s_wifi_state.connect_queue=(void *)1;s_wifi_state.connect_future_registered=true;s_wifi_state.connect_future_token=7;s_wifi_state.connect_generation=1;
    wifi_queue_connect_event(1,1,0,NULL);
    assert(writes==1 && !wakes && !notifications);
    s_wifi_runtime=(void *)1;
    wifi_queue_connect_event(1,1,0,NULL);
    assert(writes==2 && wakes==1 && notifications==1);
}
