
int main(void) {
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_generation=8;
    wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};
    atomic_store(&s_wifi_timeout_armed_generation,7);emit_old_timeout=true;timer_stop_error=-9;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-9);
    assert(configs==0 && connects==0 && arms==0 && barriers==1);
    assert(s_wifi_state.status.connect_timer_error==-9);
    timer_stop_error=0;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    assert(configs==1 && connects==1 && arms==1 && barriers==2);
    assert(!atomic_load(&s_wifi_timeout_pending));
    assert(atomic_load(&s_wifi_timeout_armed_generation)==8);
    assert(s_wifi_state.status.connect_timer_error==0);
}
