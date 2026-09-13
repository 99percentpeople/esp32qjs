
int main(void) {
    esp32_mquickjs_wifi_rrm_sdk_result_t r;
    for(dispatch_mode=1;dispatch_mode<=2;dispatch_mode++) {
        assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_SUBMIT,callback,&identity,&r)==ESP_FAIL);
        assert(!r.entered && !r.tx_attempted && r.ownership==-1 && !calls && !queries);
    }
    dispatch_mode=0;native_rc=-12;native_ownership=0;
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_SUBMIT,callback,&identity,&r)==ESP_OK);
    assert(r.entered && r.tx_attempted && r.code==-12 && !r.ownership && calls==1 && queries==1);
    native_ownership=-16;native_rc=-16;
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_CANCEL,callback,&identity,&r)==ESP_OK);
    assert(r.entered && !r.tx_attempted && r.code==-16 && r.ownership==-16);
    native_ownership=1;
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,callback,&identity,&r)==ESP_OK);
    assert(r.code==1 && r.ownership==1 && !r.tx_attempted && calls==2 && queries==4);
    assert(esp32_mquickjs_wifi_rrm_sdk_command(-1,callback,&identity,&r)==ESP_ERR_INVALID_ARG);
    assert(!r.entered && r.ownership==-1);
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,NULL,&identity,&r)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,callback,NULL,&r)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,callback,&identity,NULL)==ESP_ERR_INVALID_ARG);
    return 0;
}
