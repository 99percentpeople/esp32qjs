
int main(void){
 bool rebuild=true;assert(!esp32_mquickjs_wifi_radio_wapi_prepare(false,&rebuild)&&!rebuild);
 s_radio.leases[1].identity=55;
 assert(esp32_mquickjs_wifi_radio_wapi_prepare(true,&rebuild)==ESP_ERR_INVALID_STATE&&!rebuild);
 assert(!s_wapi.requested_enabled);s_radio.leases[1].identity=0;
 assert(!__wrap_esp_wifi_internal_wapi_init());s_radio.driver_owned=true;s_radio.driver_state=2;snapshot_unchanged=true;
 assert(!esp32_mquickjs_wifi_radio_wapi_prepare(true,&rebuild)&&rebuild&&!s_wapi.requested_enabled);
 esp32_mquickjs_wifi_radio_lifecycle_t token={.identity=41};
 assert(esp32_mquickjs_wifi_radio_wapi_select(&token,1,true)==ESP_ERR_INVALID_STATE&&!s_wapi.requested_enabled);
 checkpoint_exact=true;stopped_fenced=false;
 assert(esp32_mquickjs_wifi_radio_wapi_select(&token,1,true)==ESP_ERR_INVALID_STATE&&!s_wapi.requested_enabled);
 stopped_fenced=true;assert(!esp32_mquickjs_wifi_radio_wapi_select(&token,1,true)&&s_wapi.requested_enabled&&!inits);
 assert(!__wrap_esp_wifi_internal_wapi_deinit()&&!deinits);
 assert(!__wrap_esp_wifi_internal_wapi_init()&&inits==1);assert(!__wrap_esp_wifi_internal_wapi_deinit());
 assert(!radio_locked&&!lock_depth);
}
