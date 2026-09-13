
int main(void){
 s_radio.generation=7;s_radio.operation=(esp32_mquickjs_wifi_radio_operation_t){7,3,5,ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN};
 s_nan_radio=calloc(1,sizeof(*s_nan_radio));assert(s_nan_radio);
 s_nan_radio->lease=(esp32_mquickjs_wifi_radio_lease_t){7,3,0,true};s_nan_radio->status.ready=true;
 esp32_mquickjs_wifi_radio_operation_t token=s_radio.operation,stale=token;--stale.identity;
 wifi_nan_publish_cfg_t config={1};uint8_t id=0;
 assert(esp32_mquickjs_wifi_radio_nan_service_start(&stale,&config,NULL,&id)==ESP_ERR_INVALID_STATE&&!id&&!publish_calls);
 assert(!esp32_mquickjs_wifi_radio_nan_service_start(&token,&config,NULL,&id)&&id==7&&publish_calls==1);
 bool cancelled=false;cancel_error=-71;
 assert(esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)==-71&&!cancelled&&cancel_calls==1);
 cancel_error=0;assert(esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)==ESP_ERR_TIMEOUT&&cancelled&&cancel_calls==2);
 assert(esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)==ESP_ERR_TIMEOUT&&cancel_calls==2);
 tx_drained=true;resume_error=-72;assert(esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)==-72&&cancel_calls==2);
 resume_error=0;assert(!esp32_mquickjs_wifi_radio_nan_service_close(&token,id,&cancelled)&&cancel_calls==2);
 assert(esp32_mquickjs_wifi_radio_nan_service_close(&stale,id,&cancelled)==ESP_ERR_INVALID_STATE&&cancel_calls==2);
 free(s_nan_radio);assert(!mutation_locked&&!critical);
}
