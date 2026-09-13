
int main(void){
    /* New policy flags occupy existing padding, preserving this ABI/budget. */
    assert(sizeof(esp32_mquickjs_wifi_eap_sdk_snapshot_t)==16);
    esp32_mquickjs_wifi_eap_sdk_snapshot_t out;
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(NULL)==ESP_ERR_INVALID_ARG);
    assert(!calls && !dispatches);
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&out)==ESP_OK);
    assert(out.entered && out.time_check_known && !out.disable_time_check);
    assert(out.resources==13 && out.cleanup_error==71 && out.control_error==72);
    time_disabled=true;on_wifi=true;
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&out)==ESP_OK);
    assert(out.time_check_known && out.disable_time_check && dispatches==1);
    getter_error=73;
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&out)==ESP_OK);
    assert(out.entered && !out.time_check_known && out.resources==13);
    on_wifi=false;dispatch_failure=true;
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&out)==ESP_FAIL);
    assert(!out.entered && !out.time_check_known && !out.disable_time_check);
    assert(out.resources==UINT32_MAX && calls==3);
    return 0;
}
