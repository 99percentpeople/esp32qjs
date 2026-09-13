
int main(void){
 uint8_t*m=metadata;memcpy(buffer+56,&m,sizeof(m));metadata[9]=7;
 __wrap_nan_naf_txcb(buffer);assert(calls==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
 __wrap_nan_naf_txcb(buffer);assert(calls==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 __wrap_nan_naf_txcb(buffer);assert(calls==2);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
}
