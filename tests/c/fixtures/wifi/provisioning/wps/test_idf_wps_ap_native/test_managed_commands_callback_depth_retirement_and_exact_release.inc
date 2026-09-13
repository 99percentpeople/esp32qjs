
int main(void){
 esp_wps_config_t cfg={WPS_TYPE_PBC};uint32_t id=0,revision=0;
 assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==0 && id && enables==1);
 assert(!esp32qjs_wps_ap_command_allowed(0) && esp32qjs_wps_ap_command_allowed(id));
 assert(!esp32qjs_wps_ap_command_allowed(id+1));
 assert(esp32qjs_wps_ap_native_start(id+1)==ESP_ERR_INVALID_STATE && !starts);
 assert(esp32qjs_wps_ap_native_start(id)==0 && starts==1);
 assert(esp32qjs_wps_ap_native_start(id)==ESP_ERR_INVALID_STATE && starts==1);
 assert(esp32qjs_wps_ap_native_retire(id)==ESP_ERR_INVALID_STATE && !deinits);
 assert(esp32qjs_wps_ap_result_activity_enter(id)==&hapd);
 assert(esp32qjs_wps_ap_native_stop(id)==ESP_ERR_INVALID_STATE && !deinits);
 esp32qjs_wps_ap_result_callback_leave(id);
 assert(esp32qjs_wps_ap_native_stop(id)==0);
 assert(!esp32qjs_wps_ap_result_deinit_allowed(&hapd));
 deinit_error=ESP_ERR_NOT_FINISHED;
 assert(esp32qjs_wps_ap_native_retire(id)==ESP_ERR_NOT_FINISHED && live==1);
 assert(!esp32qjs_wps_ap_result_deinit_allowed(&hapd) && !factory_calls);
 deinit_error=0;assert(esp32qjs_wps_ap_native_retire(id)==0 && deinits==2);
 assert(live==1 && !s_wps_ap_result->status.sdk_attached && factory_calls==1);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(!esp32qjs_wps_ap_result_activity_enter(id)); /* Rejected late activity. */
 assert(esp32qjs_wps_ap_native_release(id,revision)==ESP_ERR_NOT_FINISHED && live==1);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 uint32_t next=0;assert(esp32qjs_wps_ap_native_begin(&cfg,&next)==0 && next>id);
 assert(!esp32qjs_wps_ap_command_allowed(id));
 assert(esp32qjs_wps_ap_native_stop(id)==ESP_ERR_INVALID_STATE);
 assert(!s_wps_ap_result->status.closing);
 assert(esp32qjs_wps_ap_native_stop(next)==0 && esp32qjs_wps_ap_native_retire(next)==0);
 assert(esp32qjs_wps_ap_native_checkpoint(next,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(next,revision)==0 && !live && !posts);
 return 0;
}
