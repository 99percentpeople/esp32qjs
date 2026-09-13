
int main(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};uint64_t id=0;
    ap_reserved=true;
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_ERR_INVALID_STATE);
    assert(!id && !allocations && !enable_calls && !s_wps_native);
    ap_reserved=false;
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_OK && id && allocations==1);
    fixture_dispose();return 0;
}
