
int main(void){
 for(int failure=-1;failure>=-3;--failure){
  native_init_error=failure;assert(__wrap_esp_wifi_internal_wapi_init()==failure);
  esp32_mquickjs_wifi_wapi_status_t s;esp32_mquickjs_wifi_wapi_sdk_status(&s);
  assert(s.error==failure&&!s.enabled&&!s.supplicant_active&&!s.uncertain);
  assert(!__wrap_esp_wifi_internal_wapi_deinit()&&!deinits);
 }
 assert(inits==3&&!lock_depth);
}
