
int main(void) {
 uint32_t id=0;int context=1;
 station_held=true;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_INVALID_STATE);
 station_held=false;native_owner=1;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_INVALID_STATE);
 native_owner=0;gWpsSm=&context;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_INVALID_STATE);
 gWpsSm=NULL;on_native=false;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_INVALID_STATE);
 on_native=true;oom=true;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_NO_MEM && !id && !live);
 oom=false;s_wps_ap_result_last_identity=UINT32_MAX-1;
 assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_OK && id==UINT32_MAX);
 assert(esp32qjs_wps_ap_result_close_intent(id)==ESP_OK);
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_wps_ap_result_discard_unbound(id)==ESP_OK && !live);
 id=0;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_NO_MEM && !id);
 assert(!esp32qjs_wps_ap_result_held() && !posts);
 return 0;
}
