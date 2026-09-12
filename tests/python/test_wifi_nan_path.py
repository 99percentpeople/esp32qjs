"""Deferred production NAN DataPath worker/owner/receive/timeout regression cases."""
import unittest

from test_wifi_nan_session import NanSessionLifecycle


class NanDataPathLifecycle(unittest.TestCase):
    compile_case = NanSessionLifecycle.compile_case

    def test_outgoing_early_completion_and_close_wait_for_exact_native_retirement(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup(false);path_inline=true;esp32_mquickjs_wifi_nan_path_t*p=NULL;
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7},.confirm_required=true};
 assert(!esp32_mquickjs_wifi_nan_path_create(d,&req,1000,&p)&&!path_requests);
 assert(!esp32_mquickjs_wifi_nan_path_activate(p));tick();
 esp32_mquickjs_wifi_nan_path_status_t state;esp32_mquickjs_wifi_nan_path_status(p,&state);
 assert(state.ready&&state.native.accepted&&state.native.identity==100&&path_requests==1);
 esp32_mquickjs_wifi_nan_path_close(p,false);tick();assert(path_ends==1&&!p->status.retired);
 path_native[0].native_deleted=true;tick();assert(!p->status.retired&&path_releases==1);
 path_native[0].frames_retired=true;tick();assert(p->status.retired&&path_releases==2);
 assert(!s->status.closing&&!s->paths[0]&&!s->paths[1]);
 esp32_mquickjs_wifi_nan_path_release(p);finish();
}
''')

    def test_incoming_reservation_rollback_and_explicit_response_ignore_full_observation_queue(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup(true);q.full=true;incoming();tick();
 esp32_mquickjs_wifi_nan_path_t*p=NULL,*other=NULL;
 assert(!esp32_mquickjs_wifi_nan_path_receive(d,&p)&&p&&!p->status.delivered);
 assert(!esp32_mquickjs_wifi_nan_path_receive(d,&other)&&!other);
 esp32_mquickjs_wifi_nan_path_unreserve(p);esp32_mquickjs_wifi_nan_path_release(p);p=NULL;
 assert(!esp32_mquickjs_wifi_nan_path_receive(d,&p)&&p);assert(esp32_mquickjs_wifi_nan_path_deliver(p));
 assert(!esp32_mquickjs_wifi_nan_path_receive(d,&other)&&!other);
 uint8_t ssi[2]={4,5};assert(!esp32_mquickjs_wifi_nan_path_respond(p,true,ssi,2));
 assert(esp32_mquickjs_wifi_nan_path_respond(p,true,ssi,2)==ESP_ERR_INVALID_STATE);
 tick();assert(path_responses==1&&path_response_accept&&!p->status.ready);
 path_native[0].accepted=true;path_native[0].completed_us=clock_us;
 esp32_mquickjs_wifi_nan_path_status_t state;esp32_mquickjs_wifi_nan_path_status(p,&state);assert(state.ready);
 esp32_mquickjs_wifi_nan_path_close(p,false);tick();
 path_native[0].native_deleted=path_native[0].frames_retired=true;tick();
 assert(p->status.retired&&!s->status.closing);esp32_mquickjs_wifi_nan_path_release(p);finish();
}
''')

    def test_unanswered_request_and_allocation_failure_decline_without_js_owner(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup(true);incoming();fail_allocation=allocation_calls+1;tick();
 assert(path_responses==1&&!path_response_accept&&!s_nan_path_handles&&!s->status.closing);
 path_native[0].native_deleted=path_native[0].frames_retired=true;tick();
 assert(!path_native[0].identity);fail_allocation=0;incoming();tick();
 assert(s_nan_path_handles==1);
 clock_us+=10000000;tick();assert(path_responses==2&&!path_response_accept);
 path_native[0].native_deleted=path_native[0].frames_retired=true;tick();assert(!s_nan_path_handles);
 finish();
}
''')

    def test_public_timeout_preserves_cleanup_and_failed_native_end_closes_parent(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup(false);esp32_mquickjs_wifi_nan_path_t*p=NULL;
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7},.confirm_required=true};
 assert(!esp32_mquickjs_wifi_nan_path_create(d,&req,250,&p));
 assert(!esp32_mquickjs_wifi_nan_path_activate(p));tick();
 clock_us+=250000;path_end_error=-73;close_error=ESP_ERR_TIMEOUT;tick();
 assert(p->status.timed_out&&p->status.closing&&!p->status.retired);
 assert(path_ends==1&&s->status.closing&&!s->status.retired);
 tick();assert(path_ends==1&&!p->status.retired);
 close_error=0;tick();assert(p->status.retired&&s->status.retired);
 esp32_mquickjs_wifi_nan_path_release(p);finish();
}
''')


HELPER = r'''
static esp32_mquickjs_wifi_nan_session_t*s;
static esp32_mquickjs_wifi_nan_discovery_t*d;
static struct esp32_mquickjs_event_queue q={.references=1};
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
static void setup(bool publish){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t config={.op_channel=6};
 assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
 esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="ndp",.datapath_reqd=true}};
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,publish,&cfg,1000,&d));
 assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();assert(d->status.ready);
}
void incoming(void){
 assert(!path_native[0].identity);
 path_native[0]=(esp32_mquickjs_wifi_nan_ndp_status_t){.identity=path_next_native++,.service_id=7,
  .publisher_id=7,.ndp_id=9,.peer={2,3,4,5,6,7},.native_bound=true,.host_bound=true,.started_us=clock_us};
}
static void finish(void){
 if(!s->status.retired){esp32_mquickjs_wifi_nan_session_close(s,false);tick();}
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);esp32_mquickjs_wifi_nan_discovery_release(d);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations&&!s_nan_path_handles);
}
'''
