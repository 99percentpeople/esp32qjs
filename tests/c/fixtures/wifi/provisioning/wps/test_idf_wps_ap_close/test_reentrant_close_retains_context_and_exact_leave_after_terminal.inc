
int main(void) {
 int context=1;assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 uint32_t id=esp32qjs_wps_ap_result_identity(&context);
 assert(esp32qjs_wps_ap_result_callback_enter(id)==&context);
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_TIMEOUT,NULL,0)==ESP_OK);
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_ERR_INVALID_STATE);
 assert(s_wps_ap_result->status.callback_depth==1 && live==1);
 for(unsigned i=0;i<ESP32QJS_WPS_AP_CLOSE_PREPARED;i++)assert(!cleanup_calls[i]);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(live==1 && s_wps_ap_result->status.sdk_attached);
 esp32qjs_wps_ap_result_callback_leave(id+1);assert(s_wps_ap_result->status.callback_depth==1);
 esp32qjs_wps_ap_result_callback_leave(id);assert(!s_wps_ap_result->status.callback_depth);
 assert(!esp32qjs_wps_ap_result_callback_enter(id));
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
 assert(s_wps_ap_result->status.error==ESP_ERR_TIMEOUT && !s_wps_ap_result->status.cleanup_error);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 return 0;
}
