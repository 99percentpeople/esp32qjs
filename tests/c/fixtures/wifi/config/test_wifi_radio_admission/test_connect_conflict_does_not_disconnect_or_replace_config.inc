
int main(void) {
    admission_error=ESP_ERR_INVALID_STATE;s_wifi_state.status.connected=true;
    s_wifi_state.connect_timeout_timer=(void *)1;
    wifi_config_t config={.sta.ssid="other-network",.sta.pmf_cfg.capable=true};
    assert(esp32_mquickjs_wifi_start_connect(&config,50)==ESP_ERR_INVALID_STATE);
    assert(!disconnect_calls && !configs && !connects && s_wifi_state.status.connected);
}
