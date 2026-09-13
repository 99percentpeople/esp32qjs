
int main(void) {
 int context=1,foreign=2;uint8_t pin[8]={1,2,3,4,5,6,7,8};
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 uint32_t id=esp32qjs_wps_ap_result_identity(&context);
 assert(esp32qjs_wps_ap_result_event(&foreign,WIFI_EVENT_AP_WPS_RG_TIMEOUT,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_PIN,pin,8)==ESP_OK);
 assert(esp32qjs_wps_ap_result_close_intent(id)==ESP_OK && live==1 && s_wps_ap_result->status.sdk_attached);
 assert(!s_wps_ap_result->status.pin_available && !esp32qjs_wps_ap_result_context(id));
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_TIMEOUT,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(!s_wps_ap_result->status.terminal); /* Intent is not native completion. */
 esp32qjs_wps_ap_result_detach(&foreign,ESP_FAIL);assert(live==1);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 for(int fault=0;fault<3;fault++) {
  assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
  wifi_event_ap_wps_rg_fail_reason_t bad={WPS_AP_FAIL_REASON_MAX,{0}};
  int ret=fault==0?esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_PIN,pin,7):
          fault==1?esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_FAILED,&bad,sizeof(bad)):
                   esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_TIMEOUT,pin,8);
  assert(ret==ESP_ERR_INVALID_SIZE && s_wps_ap_result->status.terminal);
  assert(s_wps_ap_result->status.error==ESP_ERR_INVALID_SIZE && !s_wps_ap_result->status.pin_available);
  esp32qjs_wps_ap_result_detach(&context,ESP_OK);
 }
 assert(!live);return 0;
}
