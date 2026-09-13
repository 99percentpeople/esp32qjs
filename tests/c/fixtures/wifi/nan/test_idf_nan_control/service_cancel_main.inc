
int main(void){
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 ndp_busy=true;
 assert(esp32_mquickjs_wifi_nan_sdk_cancel_service(7)==ESP_ERR_INVALID_STATE&&!cancel_calls&&!resets);
 esp32qjs_nan_service_guard_t unfrozen=esp32qjs_nan_service_enter(7);assert(unfrozen.entered);
 esp32qjs_nan_service_leave(&unfrozen);ndp_busy=false;
 esp32qjs_nan_service_guard_t entered=esp32qjs_nan_service_enter(7);assert(entered.entered);
 assert(esp32_mquickjs_wifi_nan_sdk_cancel_service(7)==ESP_ERR_TIMEOUT&&!cancel_calls&&!resets);
 esp32qjs_nan_service_leave(&entered);
 assert(esp32_mquickjs_wifi_nan_sdk_cancel_service(7)==-72&&cancel_calls==1&&!resets&&!depth);
 assert(!esp32qjs_nan_service_enter(7).entered);
 cancel_result=0;assert(!esp32_mquickjs_wifi_nan_sdk_cancel_service(7)&&cancel_calls==2&&resets==1&&!depth);
 assert(!esp32qjs_nan_service_enter(7).entered); /* successful cancel retains freeze for TX retirement */
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(7));
 entered=esp32qjs_nan_service_enter(7);assert(entered.entered);esp32qjs_nan_service_leave(&entered);
 assert(esp32_mquickjs_wifi_nan_sdk_cancel_service(0)==ESP_ERR_INVALID_ARG&&cancel_calls==2);
}
