
int main(void){
 assert(!__wrap_esp_wifi_internal_wapi_init());native_deinit_error=-74;
 assert(__wrap_esp_wifi_internal_wapi_deinit()==-74);
 assert(esp32_mquickjs_wifi_wapi_sdk_cleanup_error()==-74);
 assert(__wrap_esp_wifi_internal_wapi_deinit()==ESP_ERR_INVALID_STATE&&deinits==1);
 assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_INVALID_STATE&&inits==1);
 assert(esp32_mquickjs_wifi_wapi_sdk_policy(false)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_wapi_status_t s;esp32_mquickjs_wifi_wapi_sdk_status(&s);
 assert(s.uncertain&&s.cleanup_error==-74&&!s.busy);
}
