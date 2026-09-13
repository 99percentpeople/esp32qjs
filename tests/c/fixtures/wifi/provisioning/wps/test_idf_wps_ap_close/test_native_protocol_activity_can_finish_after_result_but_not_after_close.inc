
int main(void){
 int context=1;uint8_t peer[6]={2,0,0,0,0,1};
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 uint32_t id=esp32qjs_wps_ap_result_identity(&context);
 assert(esp32qjs_wps_ap_result_peer_error(id,-73,peer)==ESP_OK);
 assert(s_wps_ap_result->status.terminal && s_wps_ap_result->status.error==-73);
 assert(!esp32qjs_wps_ap_result_context(id));
 assert(esp32qjs_wps_ap_result_native_context(id)==&context);
 assert(esp32qjs_wps_ap_result_activity_enter(id)==&context);
 assert(esp32qjs_wps_ap_result_busy(&context));
 assert(esp32qjs_wps_ap_result_peer_error(id,-74,peer)==ESP_ERR_INVALID_STATE);
 assert(s_wps_ap_result->status.error==-73);
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_ERR_INVALID_STATE);
 assert(!esp32qjs_wps_ap_result_native_context(id));
 esp32qjs_wps_ap_result_callback_leave(id);
 assert(!esp32qjs_wps_ap_result_busy(&context));
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 return 0;
}
