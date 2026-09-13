
int main(void) {
    s_wifi_state.connect_generation=8;s_wifi_state.connect_future_registered=true;
    s_wifi_state.connection_future_operation=ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    s_wifi_state.connect_timeout_timer=(void *)1;
    wifi_config_t config={.sta.ssid="network",.sta.pmf_cfg.capable=true};
    config_error=-8;assert(esp32_mquickjs_wifi_start_connect(&config,50)==-8);
    assert(admissions==1 && operation_releases==1 && !connects);
    config_error=0;assert(esp32_mquickjs_wifi_start_connect(&config,50)==0);
    assert(s_wifi_state.status.connected && admissions==2 && operation_releases==2);
}
