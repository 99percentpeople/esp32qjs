
int main(void){
 assert(!esp_nan_usd_init());wifi_nan_publish_cfg_t cfg={.service_name="service",.type=NAN_PUBLISH_UNSOLICITED};
 assert(esp_nan_usd_publish(&cfg)==7&&dispatches==1&&native_calls==1&&!lock_depth);
 native_wifi=true;assert(!esp_nan_usd_cancel_service(7)&&dispatches==1&&native_calls==2);native_wifi=false;
 esp32qjs_usd_command_t stale={.kind=USD_CANCEL_SERVICE,.service=7,.identity=s_esp32qjs_usd_identity};
 assert(!esp_nan_usd_deinit()&&!esp_nan_usd_init());native_wifi=true;
 assert(esp32qjs_nan_usd_command_run(&stale,NULL)==ESP_ERR_INVALID_STATE&&native_calls==2);
 assert(!esp_nan_usd_deinit());
}
