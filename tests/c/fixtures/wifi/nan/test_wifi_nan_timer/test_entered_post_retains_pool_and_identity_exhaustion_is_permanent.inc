
static void close_during_post(void){
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT&&heap_live==1);
 assert(esp32_mquickjs_wifi_nan_tx_open()==ESP_ERR_INVALID_STATE);
}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 ETSTimer *setup=(ETSTimer*)(s_dp+1288);uint8_t ndl[580]={2};
 s_nan_timer_next_identity=UINT32_MAX;
 assert(esp32_mquickjs_wifi_nan_timer_setfn(setup,(void*)esp32qjs_nan_setup_timer_callback,ndl));
 assert((uintptr_t)timers[0].args.arg==UINT32_MAX);
 assert(esp32_mquickjs_wifi_nan_timer_arm(setup,100,false));
 timer_post_hook=close_during_post;timers[0].args.callback(timers[0].args.arg);timer_post_hook=NULL;
 __wrap_nan_ndp_setup_timeout_process(timer_queued_argument);assert(!timer_native_calls);
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
 memset(setup,0,sizeof(*setup));assert(!esp32_mquickjs_wifi_nan_tx_open());
 assert(esp32_mquickjs_wifi_nan_timer_setfn(setup,(void*)esp32qjs_nan_setup_timer_callback,ndl));
 esp32_mquickjs_wifi_nan_tx_status_t state;esp32_mquickjs_wifi_nan_tx_status(&state);
 assert(state.error==ESP_ERR_NO_MEM&&timer_creates==1);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
}
