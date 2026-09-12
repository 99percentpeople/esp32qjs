"""Deferred production NAN timer queue/reuse/cleanup cases; AST only in development."""
import unittest

from test_wifi_nan_tx import NanTx


class NanTimerIdentity(unittest.TestCase):
    compile_case = NanTx.compile_case

    def test_old_callbacks_and_queued_events_cannot_reach_reused_native_addresses(self):
        self.compile_case(r'''
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
''')

    def test_peer_delete_keeps_another_handshake_and_retains_failed_handle_after_node_free(self):
        self.compile_case(r'''
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
''')

    def test_entered_post_retains_pool_and_identity_exhaustion_is_permanent(self):
        self.compile_case(r'''
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
''')
