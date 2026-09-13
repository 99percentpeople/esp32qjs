
int main(void){
 struct hostapd_data h={0};
 for(int stage=0;stage<ESP32QJS_WPS_AP_CLOSE_PREPARED;stage++){
  memset(cleanup_calls,0,sizeof(cleanup_calls));releases=0;fail_stage=stage;
  assert(esp32qjs_wps_ap_result_bind(&h)==0);
  uint32_t identity=esp32qjs_wps_ap_result_identity(&h);
  post_error=ESP_ERR_TIMEOUT; /* Observers cannot block failure retention. */
  esp32qjs_wps_ap_init_failed(&h,-81);
  assert(!releases && live==1 && s_wps_ap_result->status.sdk_attached);
  assert(s_wps_ap_result->status.error==-81 && s_wps_ap_result->status.terminal);
  assert(s_wps_ap_result->status.closing && s_wps_ap_result->status.cleanup_error==cleanup_failure);
  assert(!esp32qjs_wps_ap_result_can_bind());
  assert(!esp32qjs_wps_ap_result_context(identity));
  fail_stage=-1;esp32qjs_wps_ap_init_failed(&h,-99);
  assert(releases==1 && !live);
  for(int i=0;i<ESP32QJS_WPS_AP_CLOSE_PREPARED;i++)assert(cleanup_calls[i]==(unsigned)(i==stage?2:1));
 }
 return 0;
}
