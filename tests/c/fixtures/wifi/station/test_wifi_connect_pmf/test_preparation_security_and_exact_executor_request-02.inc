
int main(void) {
    wifi_config_t config={0};
    assert(esp32_mquickjs_wifi_prepare_connect(NULL)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_ERR_INVALID_ARG);
    config.sta.disable_wpa3_compatible_mode=true;config.sta.pmf_cfg.required=true;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_ERR_INVALID_ARG);
    config.sta.pmf_cfg.required=false;config.sta.threshold.authmode=WIFI_AUTH_WPA3_PSK;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_ERR_INVALID_ARG);
    assert(!starts && !transactions);
    config.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;prepare_error=-70;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==-70 && transactions==1 && !starts);
    prepare_error=0;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_OK && transactions==2 && !starts);
    config.sta.pmf_cfg.capable=true;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==ESP_OK && transactions==2 && starts==1);
    prepare_error=-71;
    assert(esp32_mquickjs_wifi_prepare_connect(&config)==-71 && transactions==2 && starts==2);
}
