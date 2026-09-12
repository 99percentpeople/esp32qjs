"""Deferred production NAN SD/NDP tracker with injected native boundaries.

Host pointers model ledger ownership only. Target ABI and IRAM closure require
the configured C5 compiler/link checks and hardware acceptance separately.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'components/esp32_mquickjs'


class NanTx(unittest.TestCase):
    def test_actual_tracker_oom_quarantine_recycle_return_reuse_and_exhaustion(self):
        self.compile_case(MAIN)

    def test_message_scope_and_full_callback_retirement_survive_buffer_address_reuse(self):
        self.compile_case(MESSAGE_MAIN)

    def test_datapath_frames_share_capacity_and_wait_for_entire_native_callback(self):
        self.compile_case(DATAPATH_MAIN)

    def test_datapath_identity_early_binding_mutation_and_native_address_quarantine(self):
        self.compile_case(NDP_IDENTITY_MAIN)

    def test_pairing_auth_and_followup_share_pool_and_retire_after_producer_and_recycler(self):
        self.compile_case(PAIRING_MAIN, PAIRING_BOUNDARY, pairing=True)

    def compile_case(self, main, native='', pairing=False):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('Host C compiler unavailable')
        header = (BASE / 'internal/esp32_mquickjs_wifi_nan_pasn_sdk.h').read_text() + (BASE / 'internal/esp32_mquickjs_wifi_nan_tx.h').read_text()
        sdk = (BASE / 'internal/esp32_mquickjs_wifi_nan_sdk.h').read_text().split('#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE')[0]
        ndp = (BASE / 'internal/esp32_mquickjs_wifi_nan_ndp.h').read_text()
        timer = (BASE / 'internal/esp32_mquickjs_wifi_nan_timer.h').read_text()
        source = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_tx.c').read_text()
        source = source.replace('#include "esp32_mquickjs_wifi_nan_ndp.inc"',
            (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_ndp.inc').read_text())
        source = source.replace('#include "esp32_mquickjs_wifi_nan_timer.inc"',
            (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_timer.inc').read_text())
        source = source.replace('#include "esp32_mquickjs_wifi_nan_data.inc"',
            (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_data.inc').read_text())
        source = source.replace('#include "esp32_mquickjs_wifi_nan_pairing_tx.inc"',
            (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_tx.inc').read_text())
        code = ('#define CONFIG_ESP_WIFI_NAN_PAIRING 1\n' if pairing else '') + PREFIX + TIMER_BOUNDARY + sdk + ndp + timer + header + source
        code = re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', code, flags=re.M)
        # The Host fixture supplies native pointer layout; the original target
        # width assertion remains in production and in the C5 syntax check.
        code = code.replace('_Static_assert(sizeof(uintptr_t) == 4, "reviewed C5 NAN argument and EB layout");', '')
        code = re.sub(r'_Static_assert\(sizeof\(ETSTimer\).*?"reviewed C5 NAN timer layout"\);', '', code, flags=re.S)
        with tempfile.TemporaryDirectory() as folder:
            src, binary = Path(folder) / 'case.c', Path(folder) / 'case'
            src.write_text(INCLUDES + code + SEND_BOUNDARY + DATA_BOUNDARY + native + main)
            p = subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 0, p.stderr)
            p = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(p.returncode, 0, p.stderr)


INCLUDES = '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdlib.h>\n#include <string.h>\n'
PAIRING_BOUNDARY = r'''
static unsigned pairing_outputs;
static bool recycle_in_output;
bool current_task_is_wifi_task(void){return true;}
int __real_ieee80211_mgmt_output(void *node,void *buffer,uintptr_t subtype){
 assert(!locked&&node==(void*)3&&subtype==0xb0);++pairing_outputs;
 if(recycle_in_output){
  ic_ebuf_recycle_tx(buffer);
  assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 }
 return ESP_OK;
}
'''
PAIRING_MAIN = r'''
int main(void){
 assert(esp32_mquickjs_wifi_nan_tx_open()==ESP_OK);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(41,7,true)==ESP_OK);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(42,8,true)==ESP_ERR_INVALID_STATE);
 void *first=buffers[55];
 assert(__wrap_ieee80211_mgmt_output((void*)3,first,0xb0)==ESP_OK);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(42); /* stale leave cannot clear scope */
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 esp32_mquickjs_wifi_nan_tx_pairing_leave(41);
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(42));
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(0));
 assert(!esp32_mquickjs_wifi_nan_tx_service_drained(7));
 assert(esp32_mquickjs_wifi_nan_tx_service_drained(8));
 uint32_t old=esp32_mquickjs_wifi_nan_tx_recycling(first);
 assert(old);esp32_mquickjs_wifi_nan_tx_recycled(old);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 /* A nested recycler cannot end the producer scope or reuse its entry. */
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(41,7,true)==ESP_OK);
 recycle_in_output=true;
 assert(__wrap_ieee80211_mgmt_output((void*)3,first,0xb0)==ESP_OK);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(41);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(41));
 recycle_in_output=false;
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(42,7,false)==ESP_OK);
 void *followup=esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);
 assert(followup);uint32_t callback=esp32_mquickjs_wifi_nan_tx_callback_enter(followup);
 assert(callback);esp32_mquickjs_wifi_nan_tx_pairing_leave(42);
 ic_ebuf_recycle_tx(followup);
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(42));
 esp32_mquickjs_wifi_nan_tx_callback_leave(callback,true);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(42));
 esp_err_t result_error;int64_t completed;
 assert(esp32_mquickjs_wifi_nan_tx_pairing_settled(42,&result_error,&completed)&&!result_error&&completed==100);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(44,7,false)==ESP_OK);
 followup=esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);assert(followup);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(44);ic_ebuf_recycle_tx(followup);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_drained(44));
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_settled(44,&result_error,&completed));
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(45,7,false)==ESP_OK);
 followup=esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);assert(followup);
 callback=esp32_mquickjs_wifi_nan_tx_callback_enter(followup);assert(callback);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(45);ic_ebuf_recycle_tx(followup);
 esp32_mquickjs_wifi_nan_tx_callback_leave(callback,false);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_settled(45,&result_error,&completed)&&result_error==ESP_FAIL);
 esp32_mquickjs_wifi_nan_tx_callback_leave(callback,true);
 assert(esp32_mquickjs_wifi_nan_tx_pairing_settled(45,&result_error,&completed)&&result_error==ESP_FAIL);
 /* Auth cannot bypass the same 32-slot bound or send an untracked frame. */
 assert(esp32_mquickjs_wifi_nan_tx_pairing_enter(43,7,true)==ESP_OK);
 for(unsigned i=0;i<NAN_TX_CAPACITY;i++)assert(!__wrap_ieee80211_mgmt_output((void*)3,buffers[i],0xb0));
 unsigned sent=pairing_outputs,recycled=data_recycles;
 assert(__wrap_ieee80211_mgmt_output((void*)3,buffers[50],0xb0)==ESP_ERR_NO_MEM);
 assert(pairing_outputs==sent&&data_recycles==recycled+1);
 esp32_mquickjs_wifi_nan_tx_pairing_leave(43);
 esp32_mquickjs_wifi_nan_tx_recycled(old); /* old ticket cannot retire a reused address */
 assert(!esp32_mquickjs_wifi_nan_tx_pairing_drained(43));
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT);
 for(unsigned i=0;i<NAN_TX_CAPACITY;i++)ic_ebuf_recycle_tx(buffers[i]);
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_OK&&!heap_live);
 return 0;
}
'''
PREFIX = r'''
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 11
#define ESP_ERR_NO_MEM 12
#define ESP_ERR_TIMEOUT 13
#define ESP_ERR_INVALID_RESPONSE 14
#define ESP_ERR_INVALID_ARG 15
#define ESP_FAIL 16
#define ESP_WIFI_NAN_DATAPATH_MAX_PEERS 2
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
typedef struct {uint8_t pub_id,peer_mac[6];bool confirm_required;} wifi_nan_datapath_req_t;
#define ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL 1
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 2
#define IRAM_ATTR
typedef int esp_err_t,portMUX_TYPE;
typedef void *TaskHandle_t;
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)1;}
static int64_t esp_timer_get_time(void){return 100;}
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked,heap_live,native_calls;
static bool fail_heap,fail_native;
static unsigned next_buffer;
static uint8_t buffers[64][80],metadata[64][32];
#define portENTER_CRITICAL_SAFE(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL_SAFE(p) do{(void)(p);assert(locked);locked=0;}while(0)
static void *esp32_mquickjs_memory_wireless_calloc(const char *owner,size_t count,size_t size,int policy,int role) {
    assert(!locked&&!strcmp(owner,"wifi.nan")&&policy==1&&role==2);
    if(fail_heap)return NULL;
    void *p=calloc(count,size);assert(p);++heap_live;return p;
}
static void esp32_mquickjs_memory_payload_free(void *p){assert(!locked&&p&&heap_live);--heap_live;free(p);}
void *nan_alloc_sdf(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e) {
    assert(!locked&&a==1&&b==2&&c==3&&d==4&&e==5);++native_calls;
    if(fail_native)return NULL;
    unsigned i=next_buffer++%64;
    memset(buffers[i],0,sizeof(buffers[i]));memset(metadata[i],0,sizeof(metadata[i]));
    uint8_t *m=metadata[i];memcpy(buffers[i]+56,&m,sizeof(m));return buffers[i];
}
void *nan_alloc_action(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e) {
    assert(b&&((const uint8_t*)b)[0]==2);return nan_alloc_sdf(a,2,c,d,e);
}
'''
SEND_BOUNDARY = r'''
static void *message_buffer;
int __real_nan_send_followup_msg(uint32_t *context,void *params){
 assert(!locked&&context&&params);message_buffer=esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);
 if(!message_buffer)return -1;
 *context=(uint32_t)(uintptr_t)message_buffer;return 0;
}
'''
TIMER_BOUNDARY = r'''
typedef struct {uint32_t timer_expire,padding[3];void *timer_arg;} ETSTimer;
typedef void *esp_timer_handle_t;
typedef struct {void(*callback)(void*);void*arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
static struct {esp_timer_create_args_t args;bool live,active;} timers[16];
static unsigned timer_creates,timer_stops,timer_deletes,timer_posts,timer_native_calls;
static int timer_create_error,timer_stop_error,timer_delete_error,timer_post_error;
static void *timer_queued_argument,*timer_native_argument;
static uint8_t timer_queued_phase,timer_native_phase;
static void (*timer_post_hook)(void);
static void *timer_node,*timer_ndl;
_Alignas(8) uint8_t s_dp[1324];
void esp32qjs_nan_setup_timer_callback(void *p){(void)p;}
void esp32qjs_nan_inactivity_timer_callback(void *p){(void)p;}
void *nan_dp_get_ndl_by_bss(void *node){assert(!locked);return node==timer_node?timer_ndl:NULL;}
static esp_err_t esp_timer_create(const esp_timer_create_args_t *a,esp_timer_handle_t *h){
 assert(!locked&&a->dispatch_method==ESP_TIMER_TASK&&timer_creates<16);
 if(timer_create_error)return timer_create_error;
 unsigned i=timer_creates++;timers[i].args=*a;timers[i].live=true;*h=&timers[i];return 0;
}
static esp_err_t esp_timer_stop(esp_timer_handle_t h){
 assert(!locked&&h);++timer_stops;if(timer_stop_error)return timer_stop_error;
 for(unsigned i=0;i<timer_creates;i++)if(h==&timers[i]){assert(timers[i].live);timers[i].active=false;return 0;}
 assert(false);return ESP_FAIL;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t h){
 assert(!locked&&h);++timer_deletes;if(timer_delete_error)return timer_delete_error;
 for(unsigned i=0;i<timer_creates;i++)if(h==&timers[i]){assert(timers[i].live&&!timers[i].active);timers[i].live=false;return 0;}
 assert(false);return ESP_FAIL;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t h,uint64_t us){
 assert(!locked&&h&&us);for(unsigned i=0;i<timer_creates;i++)if(h==&timers[i]){assert(timers[i].live);timers[i].active=true;return 0;}
 assert(false);return ESP_FAIL;
}
int ieee80211_timer_process(int signal,int phase,void *argument){
 assert(!locked&&signal==7&&(phase==42||phase==43)&&argument);++timer_posts;
 timer_queued_argument=argument;timer_queued_phase=phase;if(timer_post_hook)timer_post_hook();return timer_post_error;
}
void esp32_mquickjs_wifi_nan_sdk_timer_process(uint8_t phase,void *argument){
 assert(!locked&&argument);++timer_native_calls;timer_native_argument=argument;timer_native_phase=phase;
}
'''
DATA_BOUNDARY = r'''
static unsigned data_posts,data_recycles;
static int data_post_error;
static bool data_post_recycles;
static void (*data_post_hook)(void *buffer);
void ic_ebuf_recycle_tx(void *buffer){
 assert(!locked&&buffer);++data_recycles;
 uint32_t ticket=esp32_mquickjs_wifi_nan_tx_recycling(buffer);
 esp32_mquickjs_wifi_nan_tx_recycled(ticket);
}
int __real_nan_dp_post_tx(void *node,void *buffer){
 assert(!locked&&buffer);(void)node;++data_posts;
 if(data_post_recycles)ic_ebuf_recycle_tx(buffer);
 if(data_post_hook)data_post_hook(buffer);
 return data_post_error;
}
'''
NDP_IDENTITY_MAIN = r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint32_t identity=0,other=0,native=0;uint8_t ndl[20]={2,3,4,5,6,7};
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7}};
 assert(!esp32_mquickjs_wifi_nan_ndp_begin(7,&req,&identity)&&identity);
 assert(esp32_mquickjs_wifi_nan_ndp_service_busy(7)&&!esp32_mquickjs_wifi_nan_ndp_service_busy(8));
 assert(!esp32_mquickjs_wifi_nan_ndp_request_enter(&other));
 assert(esp32_mquickjs_wifi_nan_ndp_request_enter(&req)==identity);
 assert(!esp32_mquickjs_wifi_nan_ndp_native_claim(ndl,9,&native)&&native==identity);
 esp32_mquickjs_wifi_nan_ndp_native_bound(native,true);
 esp32_mquickjs_wifi_nan_sdk_notice_t notice={.kind=ESP32_MQUICKJS_NAN_SDK_NDP_BOUND,
  .context=identity,.service_id=7,.ndp_id=9,.peer={2,3,4,5,6,7}};
 esp32_mquickjs_wifi_nan_ndp_notice(&notice);
 esp32_mquickjs_wifi_nan_ndp_status_t status;
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&status)&&status.host_bound&&!status.submitted);
 assert(esp32_mquickjs_wifi_nan_ndp_peer_service(ndl,3,7));
 assert(!esp32_mquickjs_wifi_nan_ndp_peer_service(ndl,3,8));
 assert(!esp32_mquickjs_wifi_nan_ndp_peer_service(ndl,4,7));
 wifi_nan_datapath_req_t second={.pub_id=4,.peer_mac={2,3,4,5,6,8}};
 assert(esp32_mquickjs_wifi_nan_ndp_begin(8,&second,&other)==ESP_ERR_INVALID_STATE&&!other);
 /* Complete before ioctl returns; submission must not clear that terminal. */
 notice.kind=ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED;notice.status=1;
 esp32_mquickjs_wifi_nan_ndp_notice(&notice);
 esp32_mquickjs_wifi_nan_ndp_request_leave(identity,0);
 esp32_mquickjs_wifi_nan_ndp_submitted(identity,0);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&status)&&status.accepted&&status.submitted);
 assert(!esp32_mquickjs_wifi_nan_ndp_begin(8,&second,&other)&&other!=identity);
 assert(!esp32_mquickjs_wifi_nan_ndp_mutation(identity,true,&status));
 esp32_mquickjs_wifi_nan_ndp_mutation_done(identity,true,-72);
 assert(esp32_mquickjs_wifi_nan_ndp_mutation(identity,true,&status)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&status)&&status.cleanup_error==-72&&!status.native_deleted);
 assert(esp32_mquickjs_wifi_nan_ndp_deleting(ndl)==identity);
 esp32_mquickjs_wifi_nan_ndp_deleted(identity);
 assert(esp32_mquickjs_wifi_nan_ndp_request_enter(&second)==other);
 memcpy(ndl,second.peer_mac,6);native=0;
 assert(esp32_mquickjs_wifi_nan_ndp_native_claim(ndl,10,&native)==ESP_ERR_INVALID_STATE&&!native);
 esp32_mquickjs_wifi_nan_ndp_request_leave(other,-73);
 esp32_mquickjs_wifi_nan_ndp_submitted(other,-73);
 assert(esp32_mquickjs_wifi_nan_ndp_status(other,&status)&&status.error==-73&&!status.accepted);
 /* Only the caller's physical STOP boundary may reclaim this quarantine. */
 assert(!esp32_mquickjs_wifi_nan_tx_close());assert(!esp32_mquickjs_wifi_nan_tx_open());
 native=0;assert(!esp32_mquickjs_wifi_nan_ndp_native_claim(ndl,11,&native)&&native!=identity&&native!=other);
 esp32_mquickjs_wifi_nan_ndp_native_bound(native,true);
 assert(!esp32_mquickjs_wifi_nan_ndp_status(identity,&status));
 assert(esp32_mquickjs_wifi_nan_ndp_mutation(identity,true,&status)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_nan_ndp_mutation(native,false,&status)==ESP_ERR_INVALID_STATE);
 notice=(esp32_mquickjs_wifi_nan_sdk_notice_t){.kind=ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST,
  .service_id=5,.ndp_id=11,.peer={2,3,4,5,6,8}};
 esp32_mquickjs_wifi_nan_ndp_notice(&notice);
 assert(!esp32_mquickjs_wifi_nan_ndp_mutation(native,false,&status)&&status.service_id==5);
 assert(esp32_mquickjs_wifi_nan_ndp_mutation(native,false,&status)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_nan_tx_close());assert(!esp32_mquickjs_wifi_nan_tx_open());
 s_nan_ndp_next_identity=UINT32_MAX;identity=0;
 assert(!esp32_mquickjs_wifi_nan_ndp_begin(7,&req,&identity)&&identity==UINT32_MAX);
 assert(!esp32_mquickjs_wifi_nan_tx_close());assert(!esp32_mquickjs_wifi_nan_tx_open());
 other=0;assert(esp32_mquickjs_wifi_nan_ndp_begin(8,&second,&other)==ESP_ERR_NO_MEM&&!other);
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live&&!locked);
}
'''
DATAPATH_MAIN = r'''
static void *action(const uint8_t peer[6],uint8_t id){
 void *b=esp32qjs_wifi_nan_alloc_action(1,(uintptr_t)peer,3,4,5);if(!b)return NULL;
 uint8_t*m;memcpy(&m,(uint8_t*)b+56,sizeof(m));m[8]=id;m[9]=7;
 esp32_mquickjs_wifi_nan_tx_submitting(b);return b;
}
int main(void){
 uint8_t peer[6]={2,3,4,5,6,7},other[6]={2,3,4,5,6,8};
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint32_t context=0;assert(!esp32_mquickjs_wifi_nan_tx_message_begin(7,&context));
 /* NDP IDs do not alias discovery service IDs or the active message lane. */
 void*b=action(peer,7);assert(b&&!s_nan_tx->message.status.ticket);
 assert(!esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 assert(esp32_mquickjs_wifi_nan_tx_datapath_drained(8,peer));
 assert(esp32_mquickjs_wifi_nan_tx_datapath_drained(7,other));
 uint32_t callback=esp32_mquickjs_wifi_nan_tx_callback_enter(b);assert(callback);
 uint32_t freed=esp32_mquickjs_wifi_nan_tx_recycling(b);assert(freed==callback);
 esp32_mquickjs_wifi_nan_tx_recycled(freed);
 assert(!esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 /* A new frame can reuse the EB address while the previous callback exits. */
 next_buffer=0;void*newer=action(peer,7);assert(newer==b);
 esp32_mquickjs_wifi_nan_tx_callback_leave(callback,true);
 assert(!esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 esp32_mquickjs_wifi_nan_tx_recycled(freed);
 assert(!esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 uint32_t newer_ticket=esp32_mquickjs_wifi_nan_tx_recycling(newer);assert(newer_ticket!=freed);
 esp32_mquickjs_wifi_nan_tx_recycled(newer_ticket);
 assert(esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 fail_native=true;assert(!action(peer,7));fail_native=false;
 assert(esp32_mquickjs_wifi_nan_tx_datapath_drained(7,peer));
 assert(!esp32_mquickjs_wifi_nan_tx_message_release(7));
 void*frames[32];for(unsigned i=0;i<32;++i){frames[i]=action(peer,7);assert(frames[i]);}
 assert(!esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5));
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT);
 assert(!action(peer,7));
 for(unsigned i=0;i<32;++i){uint32_t t=esp32_mquickjs_wifi_nan_tx_recycling(frames[i]);assert(t);esp32_mquickjs_wifi_nan_tx_recycled(t);}
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live&&!locked);
}
'''
MESSAGE_MAIN = r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());uint32_t context=0;
 assert(!esp32_mquickjs_wifi_nan_tx_message_begin(10,&context));
 assert(!__wrap_nan_send_followup_msg(&context,&context));
 esp32_mquickjs_wifi_nan_message_tx_status_t status;
 assert(esp32_mquickjs_wifi_nan_tx_message_status(10,&status)&&status.entered&&status.allocated&&!status.buffer_retired);
 uint32_t ticket=esp32_mquickjs_wifi_nan_tx_callback_enter(message_buffer);assert(ticket==status.ticket);
 uint32_t freeing=esp32_mquickjs_wifi_nan_tx_recycling(message_buffer);assert(freeing==ticket);
 esp32_mquickjs_wifi_nan_tx_recycled(freeing);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(10,&status)&&!status.buffer_retired&&!status.tx_done);
 assert(esp32_mquickjs_wifi_nan_tx_message_release(10)==ESP_ERR_TIMEOUT);
 esp32_mquickjs_wifi_nan_tx_callback_leave(ticket,true);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(10,&status)&&status.buffer_retired&&status.tx_done&&status.tx_succeeded&&status.retired_us>=status.completed_us);
 assert(!esp32_mquickjs_wifi_nan_tx_message_release(10));
 next_buffer=0;context=0;assert(!esp32_mquickjs_wifi_nan_tx_message_begin(11,&context));
 assert(!__wrap_nan_send_followup_msg(&context,&context));
 assert(esp32_mquickjs_wifi_nan_tx_message_status(11,&status)&&status.ticket!=ticket&&!status.tx_done);
 esp32_mquickjs_wifi_nan_tx_callback_leave(ticket,true);esp32_mquickjs_wifi_nan_tx_recycled(ticket);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(11,&status)&&!status.tx_done&&!status.buffer_retired);
 freeing=esp32_mquickjs_wifi_nan_tx_recycling(message_buffer);esp32_mquickjs_wifi_nan_tx_recycled(freeing);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(11,&status)&&!status.tx_done&&status.buffer_retired);
 assert(!esp32_mquickjs_wifi_nan_tx_message_release(11));
 context=0;fail_native=true;assert(!esp32_mquickjs_wifi_nan_tx_message_begin(12,&context));
 assert(__wrap_nan_send_followup_msg(&context,&context)==-1);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(12,&status)&&status.entered&&!status.allocated&&status.buffer_retired);
 assert(!esp32_mquickjs_wifi_nan_tx_message_release(12));
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
'''
MAIN = r'''
static void *allocate(void){return esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);}
static void identify(void *b,uint8_t id){uint8_t *m;memcpy(&m,(uint8_t*)b+56,sizeof(m));m[8]=id;esp32_mquickjs_wifi_nan_tx_submitting(b);}
static void retire(void *b){uint32_t ticket=esp32_mquickjs_wifi_nan_tx_recycling(b);assert(ticket);esp32_mquickjs_wifi_nan_tx_recycled(ticket);}
int main(void) {
    fail_heap=true;assert(esp32_mquickjs_wifi_nan_tx_open()==ESP_ERR_NO_MEM&&!heap_live&&!native_calls);
    fail_heap=false;assert(!esp32_mquickjs_wifi_nan_tx_open()&&heap_live==1);
    fail_native=true;assert(!allocate());fail_native=false;
    esp32_mquickjs_wifi_nan_tx_status_t status;esp32_mquickjs_wifi_nan_tx_status(&status);assert(!status.tracked);
    next_buffer=0;void *a=allocate(),*b=allocate();assert(a&&b&&a!=b);identify(a,7);
    assert(!esp32_mquickjs_wifi_nan_tx_service_drained(7));
    assert(!esp32_mquickjs_wifi_nan_tx_service_drained(99)); /* unsubmitted b */
    identify(b,8);assert(esp32_mquickjs_wifi_nan_tx_service_drained(99));
    uint32_t old=esp32_mquickjs_wifi_nan_tx_recycling(a);assert(old);
    assert(!esp32_mquickjs_wifi_nan_tx_service_drained(7)); /* real recycle has not returned */
    next_buffer=0;void *reused=allocate();assert(reused==a);identify(reused,9);
    esp32_mquickjs_wifi_nan_tx_recycled(old);
    assert(esp32_mquickjs_wifi_nan_tx_service_drained(7)&&!esp32_mquickjs_wifi_nan_tx_service_drained(9));
    esp32_mquickjs_wifi_nan_tx_recycled(old); /* stale completion cannot release reused address */
    assert(!esp32_mquickjs_wifi_nan_tx_service_drained(9));retire(reused);retire(b);
    assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);

    assert(!esp32_mquickjs_wifi_nan_tx_open());void *list[NAN_TX_CAPACITY];
    for(unsigned i=0;i<NAN_TX_CAPACITY;i++){list[i]=allocate();assert(list[i]);}
    unsigned before=native_calls;assert(!allocate()&&native_calls==before);
    esp32_mquickjs_wifi_nan_tx_status(&status);assert(status.tracked==NAN_TX_CAPACITY&&status.unidentified==NAN_TX_CAPACITY&&status.rejected==1);
    esp32_mquickjs_wifi_nan_tx_seal();assert(!allocate()&&native_calls==before);
    assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT&&heap_live==1);
    for(unsigned i=0;i<NAN_TX_CAPACITY;i++)retire(list[i]);
    assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);

    assert(!esp32_mquickjs_wifi_nan_tx_open());a=allocate();identify(a,7);identify(a,9);
    esp32_mquickjs_wifi_nan_tx_status(&status);assert(status.error==ESP_ERR_INVALID_RESPONSE);
    retire(a);assert(!esp32_mquickjs_wifi_nan_tx_service_drained(10));
    assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);

    s_nan_tx_next_ticket=UINT32_MAX;assert(!esp32_mquickjs_wifi_nan_tx_open());a=allocate();assert(a);
    assert(!allocate());esp32_mquickjs_wifi_nan_tx_status(&status);assert(status.identity_exhausted);
    retire(a);assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
    assert(esp32_mquickjs_wifi_nan_tx_open()==ESP_ERR_NO_MEM&&!heap_live&&!locked);
}
'''
