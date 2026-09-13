
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 s_nan_ctx.own_svc[0].type=ESP_NAN_PUBLISH;
 memcpy(native_ndl,"\2\3\4\5\6\7",6);void *ndp=NULL;
 assert(!esp32qjs_nan_ndp_alloc(native_ndl,9,&ndp));
 uint32_t identity=esp32_mquickjs_wifi_nan_ndp_deleting(native_ndl);assert(identity);
 NAN_DATA_LOCK();nan_record_new_ndl(9,7,native_ndl,ESP_WIFI_NDP_ROLE_RESPONDER,0);NAN_DATA_UNLOCK();
 esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST,7,9,1,0,native_ndl);
 uint8_t ssi[2]={4,5};response_error=-74;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_response(identity,true,ssi,513)==ESP_ERR_INVALID_ARG&&!response_calls);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_response(identity,true,ssi,2)==-74&&response_calls==1&&!depth);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_response(identity,true,ssi,2)==ESP_ERR_INVALID_STATE&&response_calls==1);
 end_error=-75;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_end(identity)==-75&&end_calls==1&&!depth);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_end(identity)==ESP_ERR_INVALID_STATE&&end_calls==1);
 esp32_mquickjs_wifi_nan_ndp_status_t state;
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&state)&&state.error==-74&&state.cleanup_error==-75);
 esp32qjs_nan_ndp_delete(native_ndl);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_end(identity)==ESP_ERR_INVALID_STATE&&end_calls==1);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
}
