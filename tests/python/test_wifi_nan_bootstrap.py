"""Deferred bootstrap cases using the production Session/Pairing/message worker.

Only the native command/callback, allocation, clock and worker dispatch boundaries
are injected. These fixtures are not RF evidence and are not run on import.
"""
import unittest
from test_wifi_nan_session import NanSessionLifecycle
from test_wifi_nan_pairing_session import HELPER


PUBLISH_HELPER = (HELPER.replace('.subscribe=', '.publish=')
                  .replace('.config.subscribe', '.config.publish')
                  .replace('->config.subscribe', '->config.publish')
                  .replace('create(s,false,&cfg', 'create(s,true,&cfg')
                  .replace('WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD', 'WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY'))

BOOTSTRAP_HELPER = r'''
static void notice(uint8_t type,uint8_t status,uint16_t methods,uint8_t peer_id){
 esp32_mquickjs_wifi_nan_sdk_bootstrap_t data={.type=type,.status=status,.methods=methods,.peer_service_id=peer_id};
 esp32_mquickjs_wifi_nan_sdk_notice_t n={.kind=ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP,
  .service_id=7,.peer={2,3,4,5,6,7},.data=&data,.size=sizeof(data)};
 native_observer(begins,&n,native_opaque);
}
static __attribute__((unused)) void incoming(void){
 notice(ESP32_MQUICKJS_NAN_BOOTSTRAP_REQUEST,ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED,
  WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD,9);
}
static __attribute__((unused)) void accepted(uint8_t peer){
 notice(ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE,ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED,
  WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY,peer);
}
static __attribute__((unused)) void recycle(bool success){
 assert(message_tx.identity);message_tx.tx_done=true;message_tx.tx_succeeded=success;
 message_tx.buffer_retired=true;message_tx.completed_us=message_tx.retired_us=clock_us;tick();
}
static __attribute__((unused)) esp32_mquickjs_wifi_nan_pairing_t*request(uint32_t timeout){
 uint8_t mac[6]={2,3,4,5,6,7};esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,timeout,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_request(p));
 assert(!esp32_mquickjs_wifi_nan_pairing_activate(p));return p;
}
'''


class NanBootstrap(unittest.TestCase):
    compile_case = NanSessionLifecycle.compile_case

    def test_bootstrap_history_is_bounded_and_close_before_submission_cannot_send(self):
        self.compile_case(HELPER + BOOTSTRAP_HELPER + r'''
int main(void){
 setup();uint8_t mac[6]={2,3,4,5,6,7};
 for(unsigned i=0;i<ESP32_MQUICKJS_NAN_BOOTSTRAP_PEERS;i++){
  esp32_mquickjs_wifi_nan_pairing_t*p=NULL;mac[5]=(uint8_t)(7+i);
  assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p));
  assert(!esp32_mquickjs_wifi_nan_pairing_request(p));assert(!esp32_mquickjs_wifi_nan_pairing_activate(p));
  esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();assert(p->status.retired);
  esp32_mquickjs_wifi_nan_pairing_release(p);
 }
 esp32_mquickjs_wifi_nan_pairing_t*p=NULL;mac[5]=99;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_request(p));
 assert(esp32_mquickjs_wifi_nan_pairing_activate(p)==ESP_ERR_NO_MEM);
 assert(!p->status.activated&&!pair_starts&&!bootstrap_sends);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_outgoing_requires_local_consent_exact_response_and_request_recycle(self):
        self.compile_case(HELPER + BOOTSTRAP_HELPER + r'''
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=request(30000);
 accepted(9);assert(!p->status.peer_responded);tick();tick();
 assert(bootstrap_sends==1&&!bootstrap_response&&!pair_starts&&p->status.bootstrap_attempted);
 accepted(8);assert(!p->status.peer_responded);
 accepted(9);assert(p->status.peer_accepted&&!p->status.confirmed);tick();assert(!pair_starts);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();assert(!pair_starts);
 recycle(true);assert(pair_starts==1&&p->status.bootstrap_sent&&!p->status.bootstrap_tx_pending);
 pair_native.authenticated=pair_native.paired=true;pair_native.completed_us=clock_us;tick();
 assert(p->status.ready&&pair_commits==1);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();assert(p->status.retired);
 esp32_mquickjs_wifi_nan_pairing_release(p);
 uint8_t mac[6]={2,3,4,5,6,7};p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_request(p));
 assert(esp32_mquickjs_wifi_nan_pairing_activate(p)==ESP_ERR_INVALID_STATE);
 accepted(9);assert(!p->status.peer_responded&&pair_starts==1);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_incoming_survives_full_observation_queue_and_receive_conversion_abort(self):
        self.compile_case(PUBLISH_HELPER + BOOTSTRAP_HELPER + r'''
int main(void){
 setup();q.full=true;incoming();assert(d->status.pairing_request_pending);
 incoming();assert(d->status.pairing_requests_dropped==1);tick();
 assert(s->pairing&&!pair_starts&&!bootstrap_sends);
 esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_receive(d,&p)&&p);
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456)==ESP_ERR_INVALID_STATE);
 uint32_t identity=p->status.identity;
 esp32_mquickjs_wifi_nan_pairing_unreserve(p);esp32_mquickjs_wifi_nan_pairing_release(p);p=NULL;
 tick();assert(s->pairing&&!s->pairing->status.closing&&d->status.pairing_request_pending);
 assert(!esp32_mquickjs_wifi_nan_pairing_receive(d,&p)&&p->status.identity==identity);
 assert(esp32_mquickjs_wifi_nan_pairing_deliver(p)&&!d->status.pairing_request_pending);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();
 assert(pair_starts==1&&!bootstrap_sends);tick();tick();
 assert(bootstrap_sends==1&&bootstrap_response&&bootstrap_accept);
 pair_native.authenticated=pair_native.paired=true;pair_native.completed_us=clock_us;tick();
 assert(!p->status.ready&&!pair_commits);recycle(true);assert(p->status.ready&&pair_commits==1);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();esp32_mquickjs_wifi_nan_pairing_release(p);
 incoming();assert(!s->pairing_request.service&&d->status.pairing_requests_dropped==2);finish();
}
''', security=True, pairing=True)

    def test_default_reject_uses_arrival_deadline_and_waits_for_reject_recycle(self):
        self.compile_case(PUBLISH_HELPER + BOOTSTRAP_HELPER + r'''
int main(void){
 setup();incoming();int64_t deadline=s->pairing_request.deadline_us;tick();
 esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_receive(d,&p)&&p);
 assert(esp32_mquickjs_wifi_nan_pairing_deliver(p));
 clock_us=deadline;tick();assert(p->status.timed_out&&!pair_starts&&!p->status.retired);
 tick();assert(bootstrap_sends==1&&bootstrap_response&&!bootstrap_accept);
 assert(!p->status.retired);recycle(true);assert(p->status.retired&&!pair_starts);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_expired_unadopted_and_allocation_failed_requests_release_capture_owner(self):
        self.compile_case(PUBLISH_HELPER + BOOTSTRAP_HELPER + r'''
int main(void){
 setup();unsigned refs=d->references;incoming();queue_full=true;
 assert(!esp32_mquickjs_wifi_nan_service());clock_us=s->pairing_request.deadline_us;
 queue_full=false;tick();assert(!s->pairing&&!s->pairing_request.service&&d->references==refs);
 assert(!d->status.pairing_request_pending&&!pair_starts&&!bootstrap_sends);
 incoming();fail_allocation=allocation_calls+1;tick();fail_allocation=0;
 assert(!s->pairing&&!s->pairing_request.service&&d->references==refs);
 assert(d->status.pairing_requests_dropped==2&&!s_nan_pairing_handles);finish();
}
''', security=True, pairing=True)

    def test_comeback_and_close_quarantine_sent_request_until_native_recycle(self):
        self.compile_case(HELPER + BOOTSTRAP_HELPER + r'''
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=request(30000);tick();tick();
 notice(ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE,ESP32_MQUICKJS_NAN_BOOTSTRAP_COMEBACK,
  WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY,9);
 assert(p->status.closing&&p->status.error==ESP_ERR_NOT_SUPPORTED);tick();
 accepted(9);assert(!p->status.peer_accepted&&!p->status.retired&&!pair_starts&&s->pairing==p);
 recycle(true);assert(p->status.retired&&!s->pairing);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_runtime_stop_retires_inflight_bootstrap_before_detaching_service(self):
        self.compile_case(HELPER + BOOTSTRAP_HELPER + r'''
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=request(30000);tick();tick();
 close_error=ESP_ERR_TIMEOUT;assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());run_queued(NULL);
 assert(s->status.closing&&!s->status.retired&&!p->status.retired&&s->message);
 close_error=0;tick();assert(s->status.retired&&p->status.retired&&!s->pairing&&!s->message&&!d->parent);
 assert(!p->bootstrap_message&&!p->status.bootstrap_tx_pending&&!pair_starts);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)
