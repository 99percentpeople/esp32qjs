
int main(void) {
    esp32_mquickjs_wifi_vendor_ie_broker_status_t status;
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(0)==ESP_ERR_INVALID_ARG && !sdk_calls);
    synchronous=true;assert(esp32_mquickjs_wifi_vendor_ie_broker_register(7)==ESP_OK && !captured);
    sdk_callback_t stale=sdk_callback;void *cookie=sdk_context;
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(7)==ESP_OK && sdk_calls==1);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(8)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_vendor_ie_broker_reset(7));
    sdk_callback(sdk_context,0,NULL,NULL,-42);assert(captured==1);
    sdk_error=77;assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(7)==77);
    esp32_mquickjs_wifi_vendor_ie_broker_status(&status);
    assert(status.uncertain && status.registered && !status.accepting && status.error==77);
    stale(cookie,0,(void *)1,(void *)1,-42);assert(captured==1);
    assert(!esp32_mquickjs_wifi_vendor_ie_broker_reset(7));
    sdk_error=0;assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(7)==ESP_OK && sdk_calls==3);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(7)==ESP_OK && sdk_calls==3);
    assert(!esp32_mquickjs_wifi_vendor_ie_broker_reset(8));
    assert(esp32_mquickjs_wifi_vendor_ie_broker_reset(7));
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(8)==ESP_OK && !s_vendor_broker.callbacks_active);
    stale(cookie,0,(void *)1,(void *)1,-42);assert(captured==1);
    unregister_inside=true;sdk_callback(sdk_context,0,NULL,NULL,-42);
    assert(captured==2 && sdk_calls==5);
    esp32_mquickjs_wifi_vendor_ie_broker_status(&status);
    assert(status.unregister_written && !status.callbacks_active && status.error==ESP_ERR_TIMEOUT);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(8)==ESP_OK && sdk_calls==5);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_reset(8));
    sdk_error=88;assert(esp32_mquickjs_wifi_vendor_ie_broker_register(9)==88);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(9)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_vendor_ie_broker_status(&status);
    assert(status.uncertain && !status.registered && !status.accepting);
    sdk_error=0;assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(9)==ESP_OK);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_reset(9));
    assert(!critical);return 0;
}
