
int main(void){
 uint32_t observer=0;assert(!esp32_mquickjs_wifi_nan_sdk_observe(observe_ndp,&notices,&observer));
 fail_event=true;deliver_confirm();
 assert(!depth&&!end_calls&&!resets&&live_ndl&&(event_bits&NDP_ACCEPTED));
 assert(accepted==1&&!posts);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 fail_event=false;deliver_confirm();assert(accepted==2&&posts==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_unobserve(&observer));
}
