"""Deferred Mesh production Session scheduling and ownership regressions.

The real SDK/config and Session sources are included. Radio calls, time and
worker dispatch are injected boundaries; no independent Session state machine.
Run only in the consolidated Wi-Fi phase, not during API implementation.
"""
import re
import unittest
from test_wifi_mesh_sdk import BASE, source as sdk_source
from test_wireless_control_regression import compile_run


def source():
    clean = lambda text: re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', text, flags=re.M)
    return (sdk_source() + TYPES +
            clean((BASE / 'internal/esp32_mquickjs_wifi_mesh_radio.h').read_text()) +
            clean((BASE / 'internal/esp32_mquickjs_wifi_mesh_session.h').read_text()) +
            clean((BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_session.c').read_text()) + NATIVE)


class WiFiMeshSession(unittest.TestCase):
    def test_command_arriving_during_poll_is_retired_before_parent_close(self):
        compile_run(self, source() + r'''
int main(void){
 test_session=open_session();enqueue_then_close=true;tick();
 assert(test_job&&test_job->status.done&&test_job->status.cancelled);
 assert(!test_job->status.dispatched&&!radio_sends&&!test_session->job);
 assert(test_session->status.retired&&!s_mesh_active&&radio_closes==1);
 esp32_mquickjs_wifi_mesh_job_release(test_job);
 esp32_mquickjs_wifi_mesh_session_release(test_session);
 assert(!s_mesh_jobs&&!s_mesh_handles&&!live_allocations);
}
''')

    def test_late_native_return_preserves_original_error_and_marks_deadline(self):
        compile_run(self, source() + r'''
int main(void){
 test_session=open_session();test_job=new_job(test_session,100);
 assert(!esp32_mquickjs_wifi_mesh_job_submit(test_job));
 radio_send_duration=200000;radio_send_error=-77;tick();
 esp32_mquickjs_wifi_mesh_job_status_t result;
 esp32_mquickjs_wifi_mesh_job_result(test_job,&result,NULL);
 assert(result.done&&result.dispatched&&result.returned&&result.timed_out);
 assert(result.native_error==-77&&result.error==ESP_ERR_TIMEOUT);
 assert(result.completed_us==native_time&&!test_session->job);
 esp32_mquickjs_wifi_mesh_job_release(test_job);
 esp32_mquickjs_wifi_mesh_session_close(test_session,false);tick();
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(!live_allocations);
}
''')

    def test_cancel_and_runtime_teardown_keep_dispatched_storage_until_return(self):
        compile_run(self, source() + r'''
int main(void){
 test_session=open_session();test_job=new_job(test_session,1000);
 assert(!esp32_mquickjs_wifi_mesh_job_submit(test_job));cancel_during_send=true;tick();
 assert(radio_sends==1&&radio_closes==1&&!test_job&&!s_mesh_jobs);
 assert(test_session->status.retired&&esp32_mquickjs_wifi_mesh_prepare_runtime_destroy());
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(!live_allocations);
}
''')

    def test_close_before_first_worker_scrubs_configuration_without_radio_start(self):
        compile_run(self, source() + r'''
int main(void){
 assert(!esp32_mquickjs_wifi_mesh_open_runtime());
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_session_create(&c,false,1000,&test_session));
 assert(!esp32_mquickjs_wifi_mesh_session_activate(test_session));
 assert(esp32_mquickjs_wifi_mesh_service()&&pending_worker);
 assert(!esp32_mquickjs_wifi_mesh_prepare_runtime_destroy());run_worker();
 assert(!radio_begins&&!radio_closes&&test_session->status.retired);
 assert(all_zero(&test_session->config,sizeof(test_session->config)));
 assert(esp32_mquickjs_wifi_mesh_prepare_runtime_destroy());
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(!live_allocations);
}
''')

    def test_read_claims_require_exact_identity_and_do_not_wrap(self):
        compile_run(self, source() + r'''
int main(void){
 test_session=open_session();uint32_t a=0,b=0,c=0;
 assert(!esp32_mquickjs_wifi_mesh_read_claim(test_session,false,&a));
 assert(!esp32_mquickjs_wifi_mesh_read_claim(test_session,true,&b));
 assert(a!=b&&esp32_mquickjs_wifi_mesh_read_claim(test_session,false,&c)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_mesh_read_release(test_session,false,b);
 assert(test_session->read_identity[0]==a);
 esp32_mquickjs_wifi_mesh_read_release(test_session,false,a);
 s_mesh_job_next=UINT32_MAX;assert(!esp32_mquickjs_wifi_mesh_read_claim(test_session,false,&c));
 assert(c==UINT32_MAX);esp32_mquickjs_wifi_mesh_read_release(test_session,false,c);c=0;
 assert(esp32_mquickjs_wifi_mesh_read_claim(test_session,false,&c)==ESP_ERR_NO_MEM);
 esp32_mquickjs_wifi_mesh_session_close(test_session,false);tick();
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(!live_allocations);
}
''')


TYPES = r'''
typedef int wifi_mode_t;
typedef struct{struct{uint32_t addr;}ip,netmask,gw;}esp_netif_ip_info_t;
typedef struct{uint32_t generation,identity;}esp32_mquickjs_wifi_radio_lifecycle_t;
typedef void(*test_worker_fn_t)(void*);
bool esp32_mquickjs_submit_background_worker(test_worker_fn_t,void*);
int64_t esp_timer_get_time(void);
'''

NATIVE = r'''
static int64_t native_time=1000,radio_send_duration;
static int radio_send_error;
static unsigned radio_begins,radio_closes,radio_sends;
static bool enqueue_then_close,cancel_during_send;
static esp32_mquickjs_wifi_mesh_session_t*test_session;
static esp32_mquickjs_wifi_mesh_job_t*test_job;
static esp32_mquickjs_wifi_mesh_radio_status_t radio_state;
static test_worker_fn_t pending_worker;static void*pending_opaque;
int64_t esp_timer_get_time(void){return native_time;}
bool esp32_mquickjs_submit_background_worker(test_worker_fn_t fn,void*p){
 assert(!lock_depth&&!pending_worker);pending_worker=fn;pending_opaque=p;return true;
}
static void run_worker(void){test_worker_fn_t fn=pending_worker;void*p=pending_opaque;
 assert(fn);pending_worker=NULL;pending_opaque=NULL;fn(p);}
static void tick(void){native_time+=30000;assert(esp32_mquickjs_wifi_mesh_service());run_worker();}
static esp32_mquickjs_wifi_mesh_job_t*new_job(esp32_mquickjs_wifi_mesh_session_t*s,uint32_t timeout){
 uint8_t bytes[3]={1,2,3};esp32_mquickjs_wifi_mesh_send_t send={.destination=ESP32_MQUICKJS_MESH_TO_ROOT,
 .protocol=MESH_PROTO_BIN,.reliable=true,.bytes=bytes,.length=3};
 esp32_mquickjs_wifi_mesh_job_t*j=NULL;
 assert(!esp32_mquickjs_wifi_mesh_job_create(s,&send,NULL,timeout,&j));memset(bytes,9,sizeof(bytes));return j;
}
static esp32_mquickjs_wifi_mesh_session_t*open_session(void){
 esp32_mquickjs_wifi_mesh_session_t*s=NULL;esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_open_runtime());
 assert(!esp32_mquickjs_wifi_mesh_session_create(&c,false,5000,&s));
 assert(!esp32_mquickjs_wifi_mesh_session_activate(s));tick();assert(s->status.ready);return s;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_begin(const esp32_mquickjs_wifi_mesh_config_t*c,bool allow,
 esp32_mquickjs_wifi_mesh_radio_token_t*t,esp32_mquickjs_wifi_mesh_radio_status_t*s){
 (void)allow;assert(!lock_depth&&c->ie_key_length==8);++radio_begins;
 *t=(esp32_mquickjs_wifi_mesh_radio_token_t){9,1};radio_state.token=*t;radio_state.ready=true;
 radio_state.native.started=radio_state.native.root=radio_state.native.native_snapshot_valid=true;
 *s=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_status(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_radio_status_t*s){assert(!lock_depth&&t->identity);*s=radio_state;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_poll(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_radio_status_t*s){
 assert(!lock_depth&&t->identity);
 if(enqueue_then_close){enqueue_then_close=false;test_job=new_job(test_session,1000);
  assert(!esp32_mquickjs_wifi_mesh_job_submit(test_job));esp32_mquickjs_wifi_mesh_session_close(test_session,false);}
 *s=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_send(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 const esp32_mquickjs_wifi_mesh_send_t*send){
 assert(!lock_depth&&t->identity&&send->length==3&&!memcmp(send->bytes,"\1\2\3",3));++radio_sends;
 if(cancel_during_send){
  esp32_mquickjs_wifi_mesh_job_cancel(test_job);esp32_mquickjs_wifi_mesh_job_release(test_job);test_job=NULL;
  assert(!esp32_mquickjs_wifi_mesh_prepare_runtime_destroy());
  assert(s_mesh_jobs==1&&live_allocations==2&&!radio_closes&&!memcmp(send->bytes,"\1\2\3",3));
 }
 native_time+=radio_send_duration;return radio_send_error;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_control(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_control_t*c){assert(!lock_depth&&t->identity);c->count=0;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_receive(const esp32_mquickjs_wifi_mesh_radio_token_t*t,bool ds){
 (void)ds;assert(!lock_depth&&t->identity);return ESP_ERR_NOT_FOUND;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_close(esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_radio_status_t*s){
 assert(!lock_depth&&t->identity&&!s_mesh_active->job);++radio_closes;
 radio_state.ready=false;radio_state.native.native_retired=true;radio_state.stopped=radio_state.restored=true;
 *t=(esp32_mquickjs_wifi_mesh_radio_token_t){0};radio_state.token=*t;*s=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_recover(esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_radio_status_t*s){return esp32_mquickjs_wifi_radio_mesh_close(t,s);}
esp_err_t esp32_mquickjs_wifi_radio_mesh_message_copy(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 bool ds,esp32_mquickjs_wifi_mesh_message_t*m,uint8_t*b,size_t n){
 (void)t;(void)ds;(void)m;(void)b;(void)n;assert(!lock_depth);return ESP_ERR_NOT_FOUND;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_message_commit(const esp32_mquickjs_wifi_mesh_radio_token_t*t,bool ds,uint32_t n){
 (void)t;(void)ds;(void)n;assert(!lock_depth);return ESP_ERR_INVALID_STATE;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_event_copy(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_notice_t*n){(void)t;(void)n;assert(!lock_depth);return ESP_ERR_NOT_FOUND;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_event_commit(const esp32_mquickjs_wifi_mesh_radio_token_t*t,uint32_t n){
 (void)t;(void)n;assert(!lock_depth);return ESP_ERR_INVALID_STATE;}
'''

NATIVE += r'''
esp_err_t esp32_mquickjs_wifi_radio_mesh_scan_commit(const esp32_mquickjs_wifi_mesh_radio_token_t*t,uint32_t id,uint32_t sequence){
 assert(!lock_depth&&t->identity&&id&&sequence);return 0;
}
'''
