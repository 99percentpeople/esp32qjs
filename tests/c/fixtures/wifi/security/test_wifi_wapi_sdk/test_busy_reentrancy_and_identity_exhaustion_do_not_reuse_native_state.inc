
int main(void){
 reenter=true;assert(!__wrap_esp_wifi_internal_wapi_init()&&inits==1);
 assert(!__wrap_esp_wifi_internal_wapi_deinit());
 s_wapi.revision=UINT32_MAX;assert(esp32_mquickjs_wifi_wapi_sdk_policy(false)==ESP_ERR_NO_MEM);
 assert(s_wapi.revision==UINT32_MAX&&s_wapi.requested_enabled);
 s_wapi.generation=UINT32_MAX;assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_NO_MEM&&inits==1);
 assert(s_wapi.generation==UINT32_MAX);
}
