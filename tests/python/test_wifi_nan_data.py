"""Deferred NAN data admission and independent retirement on the production pool.

Native EB layout is supplied by a Host boundary; target ABI/link and RF proof
remain separate. These cases are authored, not executed during API development.
"""
import unittest

from test_wifi_nan_tx import NanTx


class NanDataRetirement(unittest.TestCase):
    compile_case = NanTx.compile_case

    def test_data_quota_keeps_management_capacity_and_recycles_only_rejected_transfer(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint8_t ndl[580]={2,3,4,5,6,7};data_bind(ndl,9);
 timer_node=(void*)1;timer_ndl=ndl;
 for(unsigned i=0;i<24;i++)assert(!__wrap_nan_dp_post_tx(timer_node,data_buffer(i,false)));
 assert(data_posts==24&&!data_recycles);
 assert(__wrap_nan_dp_post_tx(timer_node,data_buffer(24,false))==ESP_ERR_NO_MEM);
 assert(data_posts==24&&data_recycles==1);
 next_buffer=32;
 for(unsigned i=0;i<8;i++){
  void *b=esp32qjs_wifi_nan_alloc_action(1,(uintptr_t)ndl,3,4,5);assert(b);
  metadata[32+i][8]=9;metadata[32+i][9]=7;esp32_mquickjs_wifi_nan_tx_submitting(b);
 }
 assert(!esp32qjs_wifi_nan_alloc_action(1,(uintptr_t)ndl,3,4,5));
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT&&heap_live==1);
 for(unsigned i=0;i<24;i++)ic_ebuf_recycle_tx(buffers[i]);
 for(unsigned i=0;i<8;i++)ic_ebuf_recycle_tx(buffers[32+i]);
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
''', native=HELPERS)

    def test_native_delete_and_recycler_return_do_not_release_entered_callback(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint8_t ndl[580]={2,3,4,5,6,7};uint32_t identity=data_bind(ndl,9);
 timer_node=(void*)1;timer_ndl=ndl;
 void *b=data_buffer(0,false);assert(!__wrap_nan_dp_post_tx(timer_node,b));
 uint32_t ticket=esp32_mquickjs_wifi_nan_tx_callback_enter(b);assert(ticket);
 assert(esp32_mquickjs_wifi_nan_tx_callback_allowed(ticket));
 esp32_mquickjs_wifi_nan_ndp_deleted(identity);
 assert(!esp32_mquickjs_wifi_nan_tx_callback_allowed(ticket));
 esp32_mquickjs_wifi_nan_ndp_status_t status;
 assert(esp32_mquickjs_wifi_nan_ndp_retire_native(identity,false,&status)==ESP_ERR_TIMEOUT);
 ic_ebuf_recycle_tx(b);
 assert(esp32_mquickjs_wifi_nan_ndp_retire_native(identity,true,&status)==ESP_ERR_TIMEOUT);
 esp32_mquickjs_wifi_nan_tx_callback_leave(ticket,false);
 assert(!esp32_mquickjs_wifi_nan_ndp_retire_native(identity,true,&status));
 assert(status.identity==identity&&status.frames_retired&&status.native_deleted);
 assert(!esp32_mquickjs_wifi_nan_ndp_service_busy(7));
 uint32_t fresh=data_bind(ndl,9);assert(fresh!=identity);
 assert(esp32_mquickjs_wifi_nan_ndp_retire_native(identity,true,&status)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_nan_tx_callback_allowed(ticket));
 assert(esp32_mquickjs_wifi_nan_ndp_status(fresh,&status)&&!status.native_deleted);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
}
''', native=HELPERS)

    def test_post_error_does_not_free_native_buffer_and_entered_post_retains_pool(self):
        self.compile_case(r'''
static void close_inside_post(void *buffer){
 (void)buffer;assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT&&heap_live==1);
}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 data_post_error=-73;void *b=data_buffer(0,true);
 assert(__wrap_nan_dp_post_tx(NULL,b)==-73&&!data_recycles);
 assert(s_nan_tx->entries[0].ticket&&!s_nan_tx->entries[0].recycled);
 ic_ebuf_recycle_tx(b);assert(!s_nan_tx->entries[0].ticket);
 data_post_error=0;data_post_recycles=true;data_post_hook=close_inside_post;
 assert(!__wrap_nan_dp_post_tx(NULL,data_buffer(1,true))&&data_posts==2);
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
''', native=HELPERS)

    def test_duplicate_cannot_recycle_owner_and_old_recycler_cannot_retire_reused_address(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());void *b=data_buffer(0,true);
 assert(!__wrap_nan_dp_post_tx(NULL,b));
 uint32_t old=esp32_mquickjs_wifi_nan_tx_recycling(b);assert(old);
 assert(!__wrap_nan_dp_post_tx(NULL,b)); /* recycler returned address to native allocator */
 esp32_mquickjs_wifi_nan_tx_recycled(old);
 assert(data_posts==2&&!data_recycles);
 esp32_mquickjs_wifi_nan_tx_status_t state;esp32_mquickjs_wifi_nan_tx_status(&state);
 assert(state.tracked==1&&!state.error);
 assert(__wrap_nan_dp_post_tx(NULL,b)==ESP_ERR_INVALID_STATE);
 assert(data_posts==2&&!data_recycles);
 ic_ebuf_recycle_tx(b);assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
''', native=HELPERS)

    def test_group_queue_stays_session_owned_and_wire_id_wrap_avoids_retained_ids(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint8_t ndl[580]={2,3,4,5,6,7};uint32_t identity=data_bind(ndl,255);
 assert(!__wrap_nan_dp_post_tx(NULL,data_buffer(0,true)));
 uint8_t selected=0;s_dp[113]=1;
 assert(!esp32_mquickjs_wifi_nan_ndp_select_native_id(255,&selected)&&selected==2);
 esp32_mquickjs_wifi_nan_ndp_deleted(identity);
 esp32_mquickjs_wifi_nan_ndp_status_t status;
 assert(!esp32_mquickjs_wifi_nan_ndp_retire_native(identity,true,&status));
 assert(esp32_mquickjs_wifi_nan_tx_service_drained(7)); /* group EB has no service ID */
 assert(!esp32_mquickjs_wifi_nan_ndp_select_native_id(255,&selected)&&selected==255);
 assert(!esp32_mquickjs_wifi_nan_ndp_select_native_id(0,&selected)&&selected==2);
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT); /* group EB still live */
 ic_ebuf_recycle_tx(buffers[0]);assert(!esp32_mquickjs_wifi_nan_tx_close());
}
''', native=HELPERS)

    def test_individual_timer_cleanup_retries_only_delete_and_keeps_other_peer_setup(self):
        self.compile_case(r'''
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
''', native=HELPERS)


HELPERS = r'''
static uint8_t data_descriptors[64][16],data_frames[64][48];
void *data_buffer(unsigned i,bool group){
 assert(i<64);memset(buffers[i],0,sizeof(buffers[i]));
 uint8_t *descriptor=data_descriptors[i],*frame=data_frames[i],*meta=metadata[i];
 memcpy(buffers[i]+4,&descriptor,sizeof(descriptor));memcpy(descriptor+4,&frame,sizeof(frame));
 memcpy(buffers[i]+56,&meta,sizeof(meta));uint16_t length=24;memcpy(buffers[i]+20,&length,2);
 frame[4]=group?1:2;return buffers[i];
}
uint32_t data_bind(uint8_t *ndl,uint8_t id){
 wifi_nan_datapath_req_t request={.pub_id=3};memcpy(request.peer_mac,ndl,6);
 uint32_t identity=0,bound=0;
 assert(!esp32_mquickjs_wifi_nan_ndp_begin(7,&request,&identity));
 assert(esp32_mquickjs_wifi_nan_ndp_request_enter(&request)==identity);
 assert(!esp32_mquickjs_wifi_nan_ndp_native_claim(ndl,id,&bound)&&bound==identity);
 esp32_mquickjs_wifi_nan_ndp_native_bound(identity,true);
 esp32_mquickjs_wifi_nan_ndp_request_leave(identity,0);esp32_mquickjs_wifi_nan_ndp_submitted(identity,0);
 esp32_mquickjs_wifi_nan_sdk_notice_t notice={.kind=ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED,.ndp_id=id};
 memcpy(notice.peer,ndl,6);esp32_mquickjs_wifi_nan_ndp_notice(&notice);return identity;
}
'''
