
int main(void){
 uint8_t peer[6]={2,1,2,3,4,5};
 esp32_mquickjs_wifi_nan_pasn_status_t status;
 assert(esp_nan_supp_pasn_responder_init(peer,UINT32_MAX,0,callback)==-1);
 assert(!allocation_calls&&!dispatches&&!active);
 unbound=true;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1&&!allocation_calls);
 unbound=false;
 for(unsigned n=1;n<=2;n++){
  fail_allocation=allocation_calls+n;
  assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1);
  assert(!active&&!allocations&&!cache_alive);
  fail_allocation=0;
 }
 for(unsigned n=1;n<=2;n++){
  fail_cache=cache_calls+n;
  assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1);
  assert(!active&&!allocations&&!cache_alive);fail_cache=0;
 }
 mac_error=-77;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1);
 assert(!allocations&&!cache_alive);mac_error=0;
 assert(!esp_nan_supp_pasn_responder_init(peer,123456,0,callback));
 assert(active&&!strcmp(active->dev_sae_pin,"123456"));
 assert(!esp32_mquickjs_wifi_nan_pasn_status(&status)&&status.active);
 uint32_t old_identity=status.identity;struct nan_pasn_data*old=active;
 void(*old_timer)(void*,void*)=timer_cb;void*old_data=timer_data;void*old_arg=timer_arg;
 assert(esp_nan_supp_pasn_responder_init(peer,654321,0,callback)==-1);
 assert(active==old&&!strcmp(active->dev_sae_pin,"123456"));
 assert(esp32_mquickjs_wifi_nan_pasn_close(old_identity+1)==ESP_ERR_INVALID_STATE&&active==old);
 pending_error=ESP_ERR_TIMEOUT;
 assert(esp32_mquickjs_wifi_nan_pasn_close(old_identity)==ESP_ERR_TIMEOUT&&active==old&&allocations);
 pending_error=0;tx_busy=true;
 assert(esp32_mquickjs_wifi_nan_pasn_close(old_identity)==ESP_ERR_TIMEOUT&&active==old&&allocations);
 tx_busy=false;assert(!esp32_mquickjs_wifi_nan_pasn_close(old_identity));
 assert(!active&&!allocations&&!cache_alive&&!timer_cb);
 assert(!esp_nan_supp_pasn_responder_init(peer,111222,0,callback));
 assert(active->framework_identity>old_identity);old=active;
 wifi_task=true;old_timer(old_data,old_arg);wifi_task=false;
 assert(active==old&&!strcmp(active->dev_sae_pin,"111222"));
 wifi_task=true;timer_cb(timer_data,timer_arg);wifi_task=false;
 assert(!active&&!allocations&&!cache_alive);
 assert(!esp32_mquickjs_wifi_nan_pasn_status(&status)&&status.error==ESP_ERR_TIMEOUT&&status.retired);
 assert(!esp32_mquickjs_wifi_nan_pasn_shutdown());
 protocol_error=-88;
 assert(esp_nan_supp_pasn_initiator_auth(peer,123456,0,callback)==-1);
 assert(!active&&!allocations&&!cache_alive);
 assert(!esp32_mquickjs_wifi_nan_pasn_status(&status)&&status.error&&status.retired);
 protocol_error=0;
 unsigned rejected_calls=allocation_calls;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1&&allocation_calls==rejected_calls);
 assert(!esp32_mquickjs_wifi_nan_pasn_shutdown());
 fail_timer=true;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1);
 assert(!active&&!allocations&&!cache_alive);fail_timer=false;
 assert(!esp32_mquickjs_wifi_nan_pasn_shutdown());
 dispatch_error=ESP_ERR_TIMEOUT;rejected_calls=allocation_calls;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1&&allocation_calls==rejected_calls);
 assert(!active&&!allocations);dispatch_error=0;
 assert(!esp_nan_supp_pasn_responder_init(peer,123456,0,callback));
 tx_busy=true;
 assert(!esp32_mquickjs_wifi_nan_pasn_shutdown()&&!active&&!allocations);
 tx_busy=false; /* Parent Radio still owns the independently tracked EBs. */
 s_esp32qjs_pasn_last_identity=UINT32_MAX;
 unsigned before=allocation_calls;
 assert(esp_nan_supp_pasn_initiator_auth(peer,123456,0,callback)==-1);
 assert(allocation_calls==before&&!active&&!allocations);
}
