
int main(void){
 for(int failure=0;failure<2;failure++){
  prepare();s_ap_prestart->requested.ap.pmf_cfg.capable=false;
  fail_pmf=failure;esp32_mquickjs_wifi_ap_prestart_result_t result;
  int error=esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result);
  assert(pmf_calls==1&&error==(failure?-92:0));
  if(failure)assert(!ap_starts&&result.ap_quiesced&&result.rollback_complete&&stored.ap.pmf_cfg.capable);
  else assert(ap_starts==1&&!stored.ap.pmf_cfg.capable&&!stored.ap.pmf_cfg.required);
  assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
 }
 wifi_config_t requested={0},previous={0};requested.ap.authmode=WIFI_AUTH_WPA3_PSK;
 assert(esp32_mquickjs_wifi_ap_prestart_prepare(9,41,&requested,&previous,matches)==ESP_ERR_NOT_SUPPORTED);
 assert(!native_allocations);
}
