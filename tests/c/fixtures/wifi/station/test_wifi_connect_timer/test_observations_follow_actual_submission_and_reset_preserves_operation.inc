
int main(void) {
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_generation=8;
    wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};
    timer_stop_error=-9;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-9);
    assert(!s_wifi_connection_counters.attempts && !connects);
    timer_stop_error=0;connect_error=-10;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-10);
    assert(s_wifi_connection_counters.attempts==1 && s_wifi_connection_counters.submission_failures==1);
    assert(!s_wifi_state.connect_in_progress && !s_wifi_connection_counters.reconnect_attempts);
    wifi_connection_note_association();connect_error=0;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    assert(s_wifi_connection_counters.attempts==2 && s_wifi_connection_counters.reconnect_attempts==1);
    unsigned armed=atomic_load(&s_wifi_timeout_armed_generation),native_calls=connects;
    esp32_mquickjs_wifi_reset_connection_counters();
    assert(s_wifi_state.connect_in_progress && atomic_load(&s_wifi_timeout_armed_generation)==armed);
    assert(s_wifi_state.connect_generation==8 && connects==native_calls);
    assert(!s_wifi_connection_counters.attempts && s_wifi_connection_counters.ever_associated);
}
