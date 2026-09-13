
int main(void){
 __wrap_nan_rx_naf((void*)1,(void*)2,(void*)3);assert(rx_calls==1&&!recycles);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
 __wrap_nan_rx_naf((void*)1,(void*)2,(void*)3);assert(rx_calls==1&&recycles==1);
 __wrap_nan_nulldata_txcb((void*)1);assert(!null_calls);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 __wrap_nan_nulldata_txcb((void*)1);assert(null_calls==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 esp32_mquickjs_wifi_nan_sdk_timer_process(42,(void*)4);assert(setup_calls==1);
 esp32_mquickjs_wifi_nan_sdk_timer_process(43,(void*)5);assert(!inactive_calls);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 esp32_mquickjs_wifi_nan_sdk_timer_process(43,(void*)5);assert(inactive_calls==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
}
