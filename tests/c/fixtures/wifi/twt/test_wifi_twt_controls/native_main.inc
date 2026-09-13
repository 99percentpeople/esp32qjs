
int main(void){
    esp32_mquickjs_wifi_twt_control_t value={.config={true,false}};
    discard=true;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value)==ESP_FAIL && !policy_writes);
    discard=false;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value)==ESP_OK && g_pm_cfg[84]==1);
    g_pm_cfg[84]=0;g_pm_cfg[85]=1;
    assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_CONFIG,&value)==ESP_OK && !value.config.post_wakeup_event && value.config.twt_enable_keep_alive);
    assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,&value)==ESP_ERR_WIFI_NOT_ASSOC);
    associate();assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,&value)==ESP_OK && value.flow_bitmap==129);
    bitmap=256;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,&value)==ESP_ERR_INVALID_RESPONSE && value.flow_bitmap==129);
    value.offset_us=102401;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value)==ESP_ERR_INVALID_ARG && !offset_writes);
    value.offset_us=102400;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value)==ESP_OK);
    corrupt=true;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value)==ESP_ERR_INVALID_RESPONSE);
    native_error=77;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value)==77);
    return 0;
}
