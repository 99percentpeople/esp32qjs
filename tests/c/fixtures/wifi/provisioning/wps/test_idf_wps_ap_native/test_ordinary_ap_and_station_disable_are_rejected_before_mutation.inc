
int main(void){
 esp_wps_config_t cfg={WPS_TYPE_PBC};uint32_t id=0,revision;
 assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==0);
 assert(wifi_ap_wps_disable_internal()==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_wps_unmanaged_disable(NULL,NULL)==ESP_ERR_INVALID_STATE);
 assert(!s_wps_ap_result->status.closing && !deinits && !cleanup_calls[0]);
 assert(esp32qjs_wps_ap_native_stop(id)==0 && esp32qjs_wps_ap_native_retire(id)==0);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 return 0;
}
