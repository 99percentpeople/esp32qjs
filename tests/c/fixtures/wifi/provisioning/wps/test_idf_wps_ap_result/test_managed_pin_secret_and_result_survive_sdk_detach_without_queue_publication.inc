
int main(void) {
 int context=1;uint32_t id=0;uint8_t pin[8]={'8','7','6','5','4','3','2','1'},out[8];
 assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_OK && id && esp32qjs_wps_ap_result_held());
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_PIN,pin,8)==ESP_OK);
 assert(!posts && esp32qjs_wps_ap_result_pin_copy(id,out)==ESP_OK && !memcmp(out,pin,8));
 assert(esp32qjs_wps_ap_result_pin_copy(id,out)==ESP_OK); /* Copy is not commit. */
 wifi_event_ap_wps_rg_fail_reason_t failure={WPS_AP_FAIL_REASON_AUTH,{2,0,0,0,0,1}};
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_FAILED,&failure,sizeof(failure))==ESP_OK);
 assert(!posts && !s_wps_ap_result->status.pin_available && !memcmp(s_wps_ap_result->pin,(uint8_t[8]){0},8));
 esp32qjs_wps_ap_result_detach(&context,0x123); /* Cleanup must not overwrite first failure. */
 esp32_mquickjs_wifi_wps_ap_result_status_t status;
 assert(esp32qjs_wps_ap_result_status(id,&status)==ESP_OK);
 assert(status.terminal && status.error==ESP_FAIL && status.failure_reason==WPS_AP_FAIL_REASON_AUTH);
 assert(!status.sdk_attached && status.retained && live==1 && esp32qjs_wps_ap_result_held());
 uint32_t next=0;assert(esp32qjs_wps_ap_result_reserve(&next)==ESP_ERR_INVALID_STATE && !next);
 assert(!esp32qjs_wps_ap_result_can_bind());
 assert(esp32qjs_wps_ap_result_discard_unbound(id)==ESP_ERR_INVALID_STATE);
 /* Fixture process teardown only; this is intentionally not a native drain
  * or a public close implementation. The production reservation stays held. */
 esp32qjs_wps_ap_result_free();assert(!live);
 return 0;
}
