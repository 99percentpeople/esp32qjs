
int main(void){
 esp_wps_config_t cfg={WPS_TYPE_PBC};uint32_t id=0,revision;
 enable_error=-81;
 assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==-81 && id && live==1);
 assert(!s_wps_ap_result->ever_bound && s_wps_ap_result->status.error==-81);
 assert(esp32qjs_wps_ap_native_stop(id)==0);
 factory_error=-82;assert(esp32qjs_wps_ap_native_retire(id)==-82 && !deinits);
 assert(s_wps_ap_result->status.error==-81 && s_wps_ap_result->status.cleanup_error==-82);
 factory_error=0;assert(esp32qjs_wps_ap_native_retire(id)==0 && !deinits);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 enable_error=0;id=0;assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==0);
 assert(esp32qjs_wps_ap_native_stop(id)==0);
 factory_error=-83;assert(esp32qjs_wps_ap_native_retire(id)==-83 && deinits==1);
 assert(!s_wps_ap_result->status.sdk_attached && s_wps_ap_result->heap_retired);
 factory_error=0;assert(esp32qjs_wps_ap_native_retire(id)==0 && deinits==1);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 return 0;
}
