
int main(void){
 esp_wps_config_t cfg={WPS_TYPE_PIN};uint32_t id=0,revision;uint8_t pin[8]={1},copy[8];
 assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==0);
 assert(esp32qjs_wps_ap_result_event(&hapd,WIFI_EVENT_AP_WPS_RG_PIN,pin,8)==0);
 assert(esp32qjs_wps_ap_result_pin_copy(id,copy)==0 && !memcmp(pin,copy,8));
 assert(esp32qjs_wps_ap_result_pin_commit(id)==0 && !s_wps_ap_result->status.pin_available);
 assert(esp32qjs_wps_ap_native_start(id)==0);
 wifi_event_ap_wps_rg_success_t event={{2,0,0,0,0,1}};
 assert(esp32qjs_wps_ap_result_event(&hapd,WIFI_EVENT_AP_WPS_RG_SUCCESS,&event,sizeof(event))==0);
 assert(esp32qjs_wps_ap_native_stop(id)==0 && esp32qjs_wps_ap_native_retire(id)==0);
 assert(s_wps_ap_result->status.terminal && s_wps_ap_result->status.error==0);
 assert(!memcmp(s_wps_ap_result->status.peer,event.peer_macaddr,6) && !posts);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 return 0;
}
