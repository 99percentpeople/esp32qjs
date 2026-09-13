
int main(void) {
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_generation=8;
    wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};timer_start_error=-10;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-10);
    assert(connects==1 && disconnects==1 && !s_wifi_state.connect_in_progress);
    assert(!atomic_load(&s_wifi_timeout_armed_generation));
    assert(s_wifi_state.status.connect_timer_error==-10);
}
