
int main(void) {
    s_wifi_state.connect_timeout_timer=(void *)1;s_wifi_state.connect_generation=8;
    wifi_config_t config={.sta.ssid="fixture"};
    stored_connect_config=config;stored_connect_config.sta.pmf_cfg.capable=true;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_ERR_INVALID_RESPONSE);
    assert(config_reads==1 && !configs && !connects && !arms);
    assert(!s_wifi_state.radio_operation.identity);
    stored_connect_config=config;config_read_error=-70;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==-70);
    assert(config_reads==2 && !configs && !connects);
    config_read_error=0;stored_connect_config.sta.password[0]='x';
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_ERR_INVALID_RESPONSE);
    assert(config_reads==3 && !configs && !connects);
    stored_connect_config=config;
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_OK);
    assert(config_reads==4 && !configs && connects==1 && arms==1);
}
