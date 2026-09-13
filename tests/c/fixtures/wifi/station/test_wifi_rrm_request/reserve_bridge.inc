
esp_err_t esp32_mquickjs_wifi_rrm_reserve(esp32_mquickjs_wifi_radio_operation_t *token) {
    return esp32_mquickjs_wifi_radio_rrm_reserve(&application,&station,&access_point,token);
}
