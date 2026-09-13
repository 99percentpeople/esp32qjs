
int main(void) {
 int context=1;uint8_t pin[8]={'1','2','3','4','5','6','7','0'},out[8]={0};
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 uint32_t first=esp32qjs_wps_ap_result_identity(&context);
 post_error=ESP_ERR_TIMEOUT;
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_PIN,pin,8)==ESP_ERR_TIMEOUT);
 assert(posts==1 && s_wps_ap_result->status.pin_available);
 assert(esp32qjs_wps_ap_result_pin_copy(first,out)==ESP_OK && !memcmp(pin,out,8));
 assert(esp32qjs_wps_ap_result_pin_commit(first)==ESP_OK);
 assert(!memcmp(s_wps_ap_result->pin,(uint8_t[8]){0},8));
 wifi_event_ap_wps_rg_success_t success={{2,3,4,5,6,7}};
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_SUCCESS,&success,sizeof(success))==ESP_ERR_TIMEOUT);
 esp32_mquickjs_wifi_wps_ap_result_status_t status;
 assert(esp32qjs_wps_ap_result_status(first,&status)==ESP_OK && status.terminal && status.error==ESP_OK);
 assert(status.event_id==WIFI_EVENT_AP_WPS_RG_SUCCESS && !memcmp(status.peer,success.peer_macaddr,6));
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_TIMEOUT,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(s_wps_ap_result->status.event_id==WIFI_EVENT_AP_WPS_RG_SUCCESS && posts==2);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);
 assert(!esp32qjs_wps_ap_result_held() && !live && clears==1);
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK); /* Same context address. */
 uint32_t second=esp32qjs_wps_ap_result_identity(&context);
 assert(second>first && !esp32qjs_wps_ap_result_context(first));
 assert(esp32qjs_wps_ap_result_context(second)==&context);
 assert(esp32qjs_wps_ap_result_close_intent(first)==ESP_ERR_INVALID_STATE);
 assert(!s_wps_ap_result->status.closing);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 return 0;
}
