
int main(void){
 uint32_t context=0;wifi_nan_followup_params_t p={.inst_id=7,.peer_inst_id=9,.peer_mac={2,3,4,5,6,7}};
 assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,false,true)==ESP_ERR_INVALID_STATE&&!submits);
 peer_exists=true;native_error=-71;
 assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,false,true)==-71&&submits==1);
 native_error=0;assert(!esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,false,true));
 assert(context==1234&&last_type==1&&last_status==0&&last_method==32&&!depth);
 assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,false,true)==ESP_ERR_INVALID_ARG);
 context=0;assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,true,true)==ESP_ERR_INVALID_STATE);
 own.type=ESP_NAN_PUBLISH;assert(!esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,true,false));
 assert(last_type==2&&last_status==1&&last_method==2);
 context=0;p.peer_mac[0]=1;assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,true,true)==ESP_ERR_INVALID_ARG);
 assert(!depth&&submits==3);return 0;
}
