
static unsigned inits,deinits;static int native_init_error,native_deinit_error;static bool reenter;
esp_err_t __real_esp_wifi_internal_wapi_init(void){
 assert(!lock_depth);++inits;
 if(reenter){assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_INVALID_STATE);
  assert(__wrap_esp_wifi_internal_wapi_deinit()==ESP_ERR_INVALID_STATE);
  assert(esp32_mquickjs_wifi_wapi_sdk_policy(false)==ESP_ERR_INVALID_STATE);}
 return native_init_error;
}
esp_err_t __real_esp_wifi_internal_wapi_deinit(void){assert(!lock_depth);++deinits;return native_deinit_error;}
