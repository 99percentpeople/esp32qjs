
bool esp32qjs_wps_ap_eapol_drained(uint32_t identity){return identity != 0;}

static unsigned cleanup_calls[ESP32QJS_WPS_AP_CLOSE_PREPARED];
static int fail_stage=-1,cleanup_failure=0x321;
esp_err_t esp32qjs_wps_ap_cleanup_step(void *context,unsigned stage){
 assert(context && stage<ESP32QJS_WPS_AP_CLOSE_PREPARED);
 assert(s_wps_ap_result->status.closing && !s_wps_ap_result->status.callback_depth);
 cleanup_calls[stage]++;return (int)stage==fail_stage?cleanup_failure:ESP_OK;
}
static int esp_event_post(int base,int32_t event,const void *data,size_t size,unsigned wait){
 assert(base==WIFI_EVENT && wait==0 && s_wps_ap_result && !s_wps_ap_result->status.retained);
 if(event==WIFI_EVENT_AP_WPS_RG_PIN)assert(s_wps_ap_result->status.pin_available && data && size==8);
 else assert(s_wps_ap_result->status.terminal && s_wps_ap_result->status.event_id==event);
 posts++;return post_error;
}
