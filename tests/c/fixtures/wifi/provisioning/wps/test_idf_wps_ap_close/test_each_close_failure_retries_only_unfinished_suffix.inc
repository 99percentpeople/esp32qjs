
int main(void) {
 int context=1;
 for(int stage=0;stage<ESP32QJS_WPS_AP_CLOSE_PREPARED;stage++) {
  memset(cleanup_calls,0,sizeof(cleanup_calls));fail_stage=stage;
  assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
  uint32_t id=esp32qjs_wps_ap_result_identity(&context);
  assert(esp32qjs_wps_ap_result_prepare_close(&context)==cleanup_failure);
  esp32_mquickjs_wifi_wps_ap_result_status_t state;
  assert(esp32qjs_wps_ap_result_status(id,&state)==ESP_OK);
  assert(state.closing && !state.close_prepared && state.cleanup_stage==stage);
  assert(state.cleanup_error==cleanup_failure && state.sdk_attached && live==1);
  assert(!esp32qjs_wps_ap_result_deinit_allowed(&context));
  assert(!esp32qjs_wps_ap_result_context(id) && !esp32qjs_wps_ap_result_can_bind());
  for(int i=0;i<ESP32QJS_WPS_AP_CLOSE_PREPARED;i++)assert(cleanup_calls[i]==(unsigned)(i<=stage));
  fail_stage=-1;assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
  assert(esp32qjs_wps_ap_result_deinit_allowed(&context));
  assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
  for(int i=0;i<ESP32QJS_WPS_AP_CLOSE_PREPARED;i++)assert(cleanup_calls[i]==(unsigned)(i==stage?2:1));
  esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 }
 return 0;
}
