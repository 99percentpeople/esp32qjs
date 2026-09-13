
int main(void){
 esp32_mquickjs_wifi_radio_operation_t token={.identity=42,.generation=3,.lease_identity=77,.kind=3};
 s_radio.operation=token;s_radio.generation=3;binding.lease=(lease_t){77,true};binding.status.ready=true;
 esp32_mquickjs_wifi_nan_query_t q={.kind=ESP32_MQUICKJS_NAN_QUERY_SERVICE,.service_id=7};
 esp32_mquickjs_wifi_nan_query_result_t out;
 assert(!esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)&&reads==1&&out.service_id==7&&!locked);
 esp32_mquickjs_wifi_radio_operation_t stale=token;--stale.identity;
 assert(esp32_mquickjs_wifi_radio_nan_query(&stale,&q,&out)==ESP_ERR_INVALID_STATE&&!out.service_id);
 stale=token;--stale.generation;assert(esp32_mquickjs_wifi_radio_nan_query(&stale,&q,&out)==ESP_ERR_INVALID_STATE);
 stale=token;--stale.lease_identity;assert(esp32_mquickjs_wifi_radio_nan_query(&stale,&q,&out)==ESP_ERR_INVALID_STATE);
 stale=token;--stale.kind;assert(esp32_mquickjs_wifi_radio_nan_query(&stale,&q,&out)==ESP_ERR_INVALID_STATE);
 binding.lease.acquired=false;assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==ESP_ERR_INVALID_STATE);
 binding.lease.acquired=true;binding.status.closing=true;
 assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==ESP_ERR_INVALID_STATE);
 binding.status.closing=false;binding.status.usd=true;
 assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==ESP_ERR_NOT_SUPPORTED&&reads==1&&!locked);
 binding.status.usd=false;native_error=-71;
 assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==-71&&reads==2&&!out.service_id&&!locked);
 s_nan_radio=NULL;assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==ESP_ERR_INVALID_STATE&&reads==2);
}
