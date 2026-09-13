
int main(void) {
    reset();wifi_scan_default_params_t value=WIFI_SCAN_PARAMS_DEFAULT_CONFIG(),actual;
    assert(esp32_mquickjs_wifi_apply_scan_parameters(&value,&actual,&result)==ESP_OK);
    return 0;
}
