
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint8_t first[580]={2,3,4,5,6,7},second[580]={2,3,4,5,6,8};
 uint32_t a=data_bind(first,9),b=data_bind(second,10);
 ETSTimer *setup=(ETSTimer*)(s_dp+1288);
 assert(esp32_mquickjs_wifi_nan_timer_setfn(setup,(void*)esp32qjs_nan_setup_timer_callback,second));
 assert(esp32_mquickjs_wifi_nan_timer_arm(setup,100,false));
 uint8_t *node=calloc(1,360);assert(node);timer_node=node;timer_ndl=first;
 ETSTimer *inactive=(ETSTimer*)(node+320);
 assert(esp32_mquickjs_wifi_nan_timer_setfn(inactive,(void*)esp32qjs_nan_inactivity_timer_callback,node));
 assert(esp32_mquickjs_wifi_nan_timer_arm(inactive,100,false));
 esp_timer_create_args_t old=timers[1].args;
 esp32_mquickjs_wifi_nan_timer_peer_delete(first,true);
 assert(esp32_mquickjs_wifi_nan_timer_disarm(inactive));
 esp32_mquickjs_wifi_nan_timer_peer_delete(first,false);free(node);
 esp32_mquickjs_wifi_nan_ndp_deleted(a);
 esp32_mquickjs_wifi_nan_ndp_status_t state;timer_delete_error=-74;
 unsigned stops=timer_stops;
 assert(esp32_mquickjs_wifi_nan_ndp_retire_native(a,true,&state)==-74);
 assert(timers[1].live&&timers[0].active);
 timer_delete_error=0;
 assert(!esp32_mquickjs_wifi_nan_ndp_retire_native(a,true,&state));
 assert(timer_stops==stops&&!timers[1].live&&timers[0].active);
 old.callback(old.arg);__wrap_nan_ndp_inactivity_timeout_process(old.arg);assert(!timer_native_calls);
 esp32_mquickjs_wifi_nan_timer_peer_delete(second,true);
 assert(esp32_mquickjs_wifi_nan_timer_disarm(setup));
 esp32_mquickjs_wifi_nan_timer_peer_delete(second,false);esp32_mquickjs_wifi_nan_ndp_deleted(b);
 assert(!esp32_mquickjs_wifi_nan_ndp_retire_native(b,true,&state)&&!setup->timer_arg);
 assert(esp32_mquickjs_wifi_nan_timer_setfn(setup,(void*)esp32qjs_nan_setup_timer_callback,second));
 assert(timer_creates==3&&timers[2].live);
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
