
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 ETSTimer *setup=(ETSTimer*)(s_dp+1288);
 uint8_t first[580]={2,3,4,5,6,7},second[580]={2,3,4,5,6,8};
 _Alignas(8) uint8_t node[360]={0};ETSTimer *inactive=(ETSTimer*)(node+320);
 assert(esp32_mquickjs_wifi_nan_timer_setfn(setup,(void*)esp32qjs_nan_setup_timer_callback,second));
 assert(esp32_mquickjs_wifi_nan_timer_arm(setup,100,false));
 unsigned stops=timer_stops;
 esp32_mquickjs_wifi_nan_timer_peer_delete(first,true);
 assert(esp32_mquickjs_wifi_nan_timer_disarm(setup)&&timer_stops==stops&&timers[0].active);
 esp32_mquickjs_wifi_nan_timer_peer_delete(first,false);
 timer_node=node;timer_ndl=first;
 assert(esp32_mquickjs_wifi_nan_timer_setfn(inactive,(void*)esp32qjs_nan_inactivity_timer_callback,node));
 assert(esp32_mquickjs_wifi_nan_timer_arm(inactive,100,false));
 esp_timer_create_args_t expired=timers[1].args;
 expired.callback(expired.arg);assert(timer_posts==1&&timer_queued_phase==43);
 esp32_mquickjs_wifi_nan_timer_peer_delete(first,true);
 assert(esp32_mquickjs_wifi_nan_timer_disarm(inactive));
 timer_delete_error=-72;assert(esp32_mquickjs_wifi_nan_timer_done(inactive));
 assert(timers[1].live&&!inactive->timer_arg);
 memset(node,0xa5,sizeof(node)); /* native node storage is no longer owned */
 esp32_mquickjs_wifi_nan_timer_peer_delete(first,false);
 __wrap_nan_ndp_inactivity_timeout_process(expired.arg);assert(!timer_native_calls);
 assert(esp32_mquickjs_wifi_nan_tx_close()==-72&&heap_live==1);
 timer_delete_error=0;stops=timer_stops;
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
 assert(timer_stops==stops); /* deletion retry does not replay successful stop */
 expired.callback(expired.arg);assert(timer_posts==1);
}
