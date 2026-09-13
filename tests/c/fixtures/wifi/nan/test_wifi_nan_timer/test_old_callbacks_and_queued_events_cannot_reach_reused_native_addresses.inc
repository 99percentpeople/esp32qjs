
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 ETSTimer *setup=(ETSTimer*)(s_dp+1288);uint8_t ndl[580]={2,3,4,5,6,7};
 assert(esp32_mquickjs_wifi_nan_timer_setfn(setup,(void*)esp32qjs_nan_setup_timer_callback,ndl));
 esp_timer_create_args_t old=timers[0].args;
 /* The actual request path performs setfn/disarm/arm in this order. */
 assert(esp32_mquickjs_wifi_nan_timer_disarm(setup));
 assert(esp32_mquickjs_wifi_nan_timer_arm(setup,100,false));
 old.callback(old.arg);assert(timer_posts==1&&timer_queued_phase==42);
 assert(timer_queued_argument==old.arg&&timer_queued_argument!=(void*)ndl);
 assert(esp32_mquickjs_wifi_nan_timer_setfn(setup,(void*)esp32qjs_nan_setup_timer_callback,ndl));
 assert(timer_creates==2&&timer_deletes==1&&timers[1].args.arg!=old.arg);
 assert(esp32_mquickjs_wifi_nan_timer_arm(setup,100,false));
 old.callback(old.arg);assert(timer_posts==1);
 __wrap_nan_ndp_setup_timeout_process(old.arg);assert(!timer_native_calls);
 timers[1].args.callback(timers[1].args.arg);
 __wrap_nan_ndp_inactivity_timeout_process(timer_queued_argument);assert(!timer_native_calls);
 __wrap_nan_ndp_setup_timeout_process(timer_queued_argument);
 assert(timer_native_calls==1&&timer_native_argument==(void*)ndl&&timer_native_phase==42);
 __wrap_nan_ndp_setup_timeout_process(timer_queued_argument);assert(timer_native_calls==1);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 old.callback(old.arg);__wrap_nan_ndp_setup_timeout_process(old.arg);
 assert(timer_posts==2&&timer_native_calls==1);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
}
