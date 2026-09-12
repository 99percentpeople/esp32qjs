"""Deferred production AP WPS result-owner and queue-pressure regressions.

Only AST-parse in the implementation wave. The entire production include is
compiled here later; allocator, SDK admission and event-post are boundaries.
No claim of native drain, public Future or RF completion.
"""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_ap_result.h'
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_result.inc'


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class WpsAPResult(unittest.TestCase):
    def run_case(self, main):
        compile_run(self, PRELUDE + declarations(HEADER.read_text()) +
                    declarations(SOURCE.read_text()) + POST + main)

    def test_queue_full_cannot_discard_terminal_or_pin_and_old_id_cannot_resolve(self):
        self.run_case(r'''
int main(void) {
 int context=1;uint8_t pin[8]={'1','2','3','4','5','6','7','0'},out[8]={0};
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 uint32_t first=esp32qjs_wps_ap_result_identity(&context);
 post_error=ESP_ERR_TIMEOUT;
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_PIN,pin,8)==ESP_ERR_TIMEOUT);
 assert(posts==1 && s_wps_ap_result->status.pin_available);
 assert(esp32qjs_wps_ap_result_pin_copy(first,out)==ESP_OK && !memcmp(pin,out,8));
 assert(esp32qjs_wps_ap_result_pin_commit(first)==ESP_OK);
 assert(!memcmp(s_wps_ap_result->pin,(uint8_t[8]){0},8));
 wifi_event_ap_wps_rg_success_t success={{2,3,4,5,6,7}};
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_SUCCESS,&success,sizeof(success))==ESP_ERR_TIMEOUT);
 esp32_mquickjs_wifi_wps_ap_result_status_t status;
 assert(esp32qjs_wps_ap_result_status(first,&status)==ESP_OK && status.terminal && status.error==ESP_OK);
 assert(status.event_id==WIFI_EVENT_AP_WPS_RG_SUCCESS && !memcmp(status.peer,success.peer_macaddr,6));
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_TIMEOUT,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(s_wps_ap_result->status.event_id==WIFI_EVENT_AP_WPS_RG_SUCCESS && posts==2);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);
 assert(!esp32qjs_wps_ap_result_held() && !live && clears==1);
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK); /* Same context address. */
 uint32_t second=esp32qjs_wps_ap_result_identity(&context);
 assert(second>first && !esp32qjs_wps_ap_result_context(first));
 assert(esp32qjs_wps_ap_result_context(second)==&context);
 assert(esp32qjs_wps_ap_result_close_intent(first)==ESP_ERR_INVALID_STATE);
 assert(!s_wps_ap_result->status.closing);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 return 0;
}
''')

    def test_managed_pin_secret_and_result_survive_sdk_detach_without_queue_publication(self):
        self.run_case(r'''
int main(void) {
 int context=1;uint32_t id=0;uint8_t pin[8]={'8','7','6','5','4','3','2','1'},out[8];
 assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_OK && id && esp32qjs_wps_ap_result_held());
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_PIN,pin,8)==ESP_OK);
 assert(!posts && esp32qjs_wps_ap_result_pin_copy(id,out)==ESP_OK && !memcmp(out,pin,8));
 assert(esp32qjs_wps_ap_result_pin_copy(id,out)==ESP_OK); /* Copy is not commit. */
 wifi_event_ap_wps_rg_fail_reason_t failure={WPS_AP_FAIL_REASON_AUTH,{2,0,0,0,0,1}};
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_FAILED,&failure,sizeof(failure))==ESP_OK);
 assert(!posts && !s_wps_ap_result->status.pin_available && !memcmp(s_wps_ap_result->pin,(uint8_t[8]){0},8));
 esp32qjs_wps_ap_result_detach(&context,0x123); /* Cleanup must not overwrite first failure. */
 esp32_mquickjs_wifi_wps_ap_result_status_t status;
 assert(esp32qjs_wps_ap_result_status(id,&status)==ESP_OK);
 assert(status.terminal && status.error==ESP_FAIL && status.failure_reason==WPS_AP_FAIL_REASON_AUTH);
 assert(!status.sdk_attached && status.retained && live==1 && esp32qjs_wps_ap_result_held());
 uint32_t next=0;assert(esp32qjs_wps_ap_result_reserve(&next)==ESP_ERR_INVALID_STATE && !next);
 assert(!esp32qjs_wps_ap_result_can_bind());
 assert(esp32qjs_wps_ap_result_discard_unbound(id)==ESP_ERR_INVALID_STATE);
 /* Fixture process teardown only; this is intentionally not a native drain
  * or a public close implementation. The production reservation stays held. */
 esp32qjs_wps_ap_result_free();assert(!live);
 return 0;
}
''')

    def test_close_intent_stale_input_malformed_payload_and_first_error(self):
        self.run_case(r'''
int main(void) {
 int context=1,foreign=2;uint8_t pin[8]={1,2,3,4,5,6,7,8};
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 uint32_t id=esp32qjs_wps_ap_result_identity(&context);
 assert(esp32qjs_wps_ap_result_event(&foreign,WIFI_EVENT_AP_WPS_RG_TIMEOUT,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_PIN,pin,8)==ESP_OK);
 assert(esp32qjs_wps_ap_result_close_intent(id)==ESP_OK && live==1 && s_wps_ap_result->status.sdk_attached);
 assert(!s_wps_ap_result->status.pin_available && !esp32qjs_wps_ap_result_context(id));
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_TIMEOUT,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(!s_wps_ap_result->status.terminal); /* Intent is not native completion. */
 esp32qjs_wps_ap_result_detach(&foreign,ESP_FAIL);assert(live==1);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 for(int fault=0;fault<3;fault++) {
  assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
  wifi_event_ap_wps_rg_fail_reason_t bad={WPS_AP_FAIL_REASON_MAX,{0}};
  int ret=fault==0?esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_PIN,pin,7):
          fault==1?esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_FAILED,&bad,sizeof(bad)):
                   esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_TIMEOUT,pin,8);
  assert(ret==ESP_ERR_INVALID_SIZE && s_wps_ap_result->status.terminal);
  assert(s_wps_ap_result->status.error==ESP_ERR_INVALID_SIZE && !s_wps_ap_result->status.pin_available);
  esp32qjs_wps_ap_result_detach(&context,ESP_OK);
 }
 assert(!live);return 0;
}
''')

    def test_admission_oom_thread_identity_exhaustion_and_unbound_discard(self):
        self.run_case(r'''
int main(void) {
 uint32_t id=0;int context=1;
 station_held=true;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_INVALID_STATE);
 station_held=false;native_owner=1;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_INVALID_STATE);
 native_owner=0;gWpsSm=&context;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_INVALID_STATE);
 gWpsSm=NULL;on_native=false;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_INVALID_STATE);
 on_native=true;oom=true;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_NO_MEM && !id && !live);
 oom=false;s_wps_ap_result_last_identity=UINT32_MAX-1;
 assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_OK && id==UINT32_MAX);
 assert(esp32qjs_wps_ap_result_close_intent(id)==ESP_OK);
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_wps_ap_result_discard_unbound(id)==ESP_OK && !live);
 id=0;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_ERR_NO_MEM && !id);
 assert(!esp32qjs_wps_ap_result_held() && !posts);
 return 0;
}
''')


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_TIMEOUT 0x107
#define WPS_OWNER_NONE 0
#define WIFI_EVENT 7
enum{WIFI_EVENT_AP_WPS_RG_PIN=10,WIFI_EVENT_AP_WPS_RG_SUCCESS,WIFI_EVENT_AP_WPS_RG_FAILED,WIFI_EVENT_AP_WPS_RG_TIMEOUT,WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP};
enum{WPS_AP_FAIL_REASON_NORMAL,WPS_AP_FAIL_REASON_CONFIG,WPS_AP_FAIL_REASON_AUTH,WPS_AP_FAIL_REASON_MAX};
typedef struct{uint8_t peer_macaddr[6];} wifi_event_ap_wps_rg_success_t;
typedef struct{int reason;uint8_t peer_macaddr[6];} wifi_event_ap_wps_rg_fail_reason_t;
static void *gWpsSm;
static bool on_native=true,station_held,oom;
static int native_owner,live,clears,posts,post_error;
bool current_task_is_wifi_task(void){return on_native;}
bool esp32qjs_wps_native_held(void){return station_held;}
static int wps_get_owner(void){return native_owner;}
static void *os_zalloc(size_t size){if(oom)return NULL;void *p=calloc(1,size);assert(p);live++;return p;}
static void forced_memzero(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static void bin_clear_free(void *p,size_t n){assert(p && live==1);forced_memzero(p,n);for(size_t i=0;i<n;i++)assert(!((uint8_t*)p)[i]);clears++;live--;free(p);}
static int esp_event_post(int base,int32_t event,const void *data,size_t size,unsigned wait);
'''

POST = r'''
bool esp32qjs_wps_ap_eapol_drained(uint32_t identity){return identity != 0;}

static unsigned cleanup_calls[ESP32QJS_WPS_AP_CLOSE_PREPARED];
static int fail_stage=-1,cleanup_failure=0x321;
esp_err_t esp32qjs_wps_ap_cleanup_step(void *context,unsigned stage){
 assert(context && stage<ESP32QJS_WPS_AP_CLOSE_PREPARED);
 assert(s_wps_ap_result->status.closing && !s_wps_ap_result->status.callback_depth);
 cleanup_calls[stage]++;return (int)stage==fail_stage?cleanup_failure:ESP_OK;
}
static int esp_event_post(int base,int32_t event,const void *data,size_t size,unsigned wait){
 assert(base==WIFI_EVENT && wait==0 && s_wps_ap_result && !s_wps_ap_result->status.retained);
 if(event==WIFI_EVENT_AP_WPS_RG_PIN)assert(s_wps_ap_result->status.pin_available && data && size==8);
 else assert(s_wps_ap_result->status.terminal && s_wps_ap_result->status.event_id==event);
 posts++;return post_error;
}
'''
