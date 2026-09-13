
int main(void){
 assert(!esp32_mquickjs_wifi_wapi_sdk_policy(false));
 assert(!__wrap_esp_wifi_internal_wapi_init()&&!inits);
 esp32_mquickjs_wifi_wapi_status_t s;esp32_mquickjs_wifi_wapi_sdk_status(&s);
 assert(s.supplicant_active&&!s.enabled&&!s.requested_enabled&&s.revision==2&&s.generation==1);
 assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_INVALID_STATE&&!inits);
 assert(!__wrap_esp_wifi_internal_wapi_deinit()&&!deinits);
 assert(!esp32_mquickjs_wifi_wapi_sdk_cleanup_error());
 assert(!esp32_mquickjs_wifi_wapi_sdk_policy(true));
 assert(!__wrap_esp_wifi_internal_wapi_init()&&inits==1);
 assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_INVALID_STATE&&inits==1);
 assert(!__wrap_esp_wifi_internal_wapi_deinit()&&deinits==1);
 assert(!__wrap_esp_wifi_internal_wapi_deinit()&&deinits==1);
 esp32_mquickjs_wifi_wapi_sdk_status(&s);assert(s.generation==2&&!s.enabled&&!s.busy&&!s.uncertain);
}
