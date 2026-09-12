"""Deferred production CHM deadline/queue/lifetime regression coverage.

Only the SDK timer, native queue and native end boundaries are controlled.
The validation, tickets, deadlines, cancel/reset and cleanup are production C.
Do not import/compile/run during the Wi-Fi implementation wave.
"""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_chm_timer.h'
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_chm_timer.c'


class ChmTimer(unittest.TestCase):
    def test_deadline_and_queue_identity_on_both_native_layouts(self):
        header = re.sub(r'^#(?:include|pragma once)[^\n]*\n', '', HEADER.read_text(), flags=re.M)
        source = re.sub(r'^#include[^\n]*\n', '', SOURCE.read_text(), flags=re.M)
        # The 64-bit host controls numeric timer handles but retains the exact
        # 20-byte legacy timer layout. Real OSI/pointer ABI is target-compiled.
        source = re.sub(r'_Static_assert\(sizeof\(uintptr_t\).*?;', '', source, count=1, flags=re.S)
        for c5 in (0, 1):
            with self.subTest(c5=c5):
                compile_run(self, '#define CONFIG_IDF_TARGET_ESP32C5 ' + str(c5) + '\n' +
                    TYPES + header + BOUNDARIES + source + MAIN)


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#undef NULL
#define NULL 0
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
#define DRAM_ATTR
#define IRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_TIMER_TASK 0
typedef int esp_err_t;
typedef uint32_t esp_timer_handle_t;
typedef void ETSTimerFunc(void *);
typedef struct {uint32_t next,timer_expire,period,fn,timer_arg;} ETSTimer;
typedef struct {void(*callback)(void*);void*arg;int dispatch_method;const char*name;}esp_timer_create_args_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical;
#define portENTER_CRITICAL_SAFE(m) do{(void)m;assert(!critical);critical++;}while(0)
#define portEXIT_CRITICAL_SAFE(m) do{(void)m;assert(critical==1);critical--;}while(0)
'''

BOUNDARIES = r'''
static uint8_t storage[128];
uint8_t *g_chm=storage;
static struct {esp_timer_create_args_t args;bool active,deleted;} handles[16];
static uint64_t now;
static unsigned creates,starts,stops,deletes,native_ends,native_starts;
static unsigned last_phase;
static int create_error,start_error,stop_error,delete_error,post_error;
static bool replace_during_post,reenter_on_end;
static uint32_t messages[16];
static unsigned messages_count;
static void rearm_max(uint64_t delay);
int64_t esp_timer_get_time(void){assert(!critical);return (int64_t)now;}
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out){
 assert(!critical&&args->dispatch_method==ESP_TIMER_TASK);if(create_error)return create_error;
 assert(++creates<16);handles[creates].args=*args;*out=creates;return 0;
}
static esp_err_t esp_timer_stop(esp_timer_handle_t handle){
 assert(!critical&&handle&&handle<=creates&&!handles[handle].deleted);stops++;
 if(stop_error)return stop_error;if(!handles[handle].active)return ESP_ERR_INVALID_STATE;
 handles[handle].active=false;return 0;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t handle,uint64_t delay){
 assert(!critical&&handle&&!handles[handle].deleted&&!handles[handle].active);(void)delay;starts++;
 if(start_error)return start_error;handles[handle].active=true;return 0;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t handle){
 assert(!critical&&handle&&!handles[handle].deleted);deletes++;
 if(delete_error)return delete_error;assert(!handles[handle].active);handles[handle].deleted=true;return 0;
}
int ieee80211_timer_process(int signal,int operation,void *argument){
 assert(!critical&&signal==7&&operation==8);
 if(replace_during_post){replace_during_post=false;rearm_max(100);}
 if(post_error)return post_error;
 assert(messages_count<16);messages[messages_count++]=(uint32_t)(uintptr_t)argument;return 0;
}
int __real_chm_start_op(void*channel,uint32_t min,uint32_t max,
 void(*start)(void*,int),void(*end)(void*,int),void*context){
 (void)channel;(void)min;(void)max;(void)start;(void)end;(void)context;
 assert(!critical);native_starts++;return 0;
}
static void original_callback(void *argument){(void)argument;assert(0);}
'''

MAIN = r'''
static ETSTimer *timer(unsigned slot){return(ETSTimer*)(g_chm+CHM_TIMER_OFFSET+20*slot);}
static void initialize(void){
 memset(storage,0,sizeof(storage));g_chm=storage;g_chm[4]=255;
 memset(handles,0,sizeof(handles));memset(s_chm_timers,0,sizeof(s_chm_timers));
 s_chm_status=(esp32qjs_wifi_chm_timer_status_t){.last_identity=1};
 creates=starts=stops=deletes=native_ends=native_starts=messages_count=0;
 create_error=start_error=stop_error=delete_error=post_error=0;now=1000;
 replace_during_post=reenter_on_end=false;
 assert(esp32qjs_wifi_chm_timer_setfn(timer(0),original_callback,NULL));
 assert(esp32qjs_wifi_chm_timer_setfn(timer(1),original_callback,(void*)1));
 assert(creates==2);
}
static void rearm_max(uint64_t delay){
 esp32qjs_wifi_chm_record_reset(g_chm+4,0,CHM_RECORD_SIZE);g_chm[4]=6;
 assert(esp32qjs_wifi_chm_timer_disarm(timer(1)));
 assert(esp32qjs_wifi_chm_timer_arm(timer(1),delay,false));
}
static void wake(unsigned slot){
 esp_timer_handle_t handle=timer(slot)->timer_arg;
 handles[handle].active=false;handles[handle].args.callback(handles[handle].args.arg);
}
void __real_chm_end_op_timeout_process(void *argument){
 assert(!critical);native_ends++;last_phase=(unsigned)(uintptr_t)argument;
 assert(!s_chm_timers[0].identity&&!s_chm_timers[1].identity);
 if(!argument)esp32qjs_wifi_chm_timer_disarm(timer(1));
 esp32qjs_wifi_chm_record_reset(g_chm+4,0,CHM_RECORD_SIZE);g_chm[4]=255;
 if(reenter_on_end){reenter_on_end=false;rearm_max(100);}
}
static void deliver(unsigned index){assert(index<messages_count);__wrap_chm_end_op_timeout_process((void*)(uintptr_t)messages[index]);}
int main(void){
 initialize();
 ETSTimer unrelated={0};
 assert(!esp32qjs_wifi_chm_timer_setfn(&unrelated,original_callback,NULL));
 assert(!esp32qjs_wifi_chm_timer_arm(&unrelated,1,false));
 assert(!esp32qjs_wifi_chm_timer_disarm(&unrelated)&&!esp32qjs_wifi_chm_timer_done(&unrelated));
 rearm_max(100);uint32_t old=s_chm_timers[1].identity;
 now+=100;wake(1);assert(messages_count==1&&messages[0]==old);
 /* Old queued expiry cannot end a later arm, even with identical channel. */
 rearm_max(200);uint32_t next=s_chm_timers[1].identity;deliver(0);
 assert(!native_ends&&s_chm_timers[1].identity==next);
 /* A late callback is merely a wake and must honor the NEW deadline. */
 chm_timer_wake((void*)1);assert(messages_count==1);
 now+=199;chm_timer_wake((void*)1);assert(messages_count==1);
 now++;chm_timer_wake((void*)1);assert(messages_count==2&&messages[1]==next);
 deliver(1);deliver(1);assert(native_ends==1&&last_phase==1);
 __wrap_chm_end_op_timeout_process(NULL);__wrap_chm_end_op_timeout_process((void*)1);
 assert(native_ends==1&&creates==2); /* no per-arm allocation */

 /* Both min/max messages may already be queued. End invalidates both before
  * a native completion callback can reenter and start another operation. */
 rearm_max(100);esp32qjs_wifi_chm_timer_arm(timer(0),50,false);
 now+=100;wake(0);wake(1);unsigned first=messages_count-2,second=messages_count-1;
 reenter_on_end=true;deliver(first);uint32_t reentered=s_chm_timers[1].identity;
 assert(native_ends==2&&!last_phase&&reentered);deliver(second);
 assert(native_ends==2&&s_chm_timers[1].identity==reentered);
 /* Post can block behind native cancellation/rearm. Its captured ticket and
  * post-failure update must never change the successor's arm. */
 now+=100;replace_during_post=true;wake(1);deliver(messages_count-1);
 assert(native_ends==2&&s_chm_timers[1].identity!=reentered);
 now+=100;post_error=-1;replace_during_post=true;wake(1);post_error=0;
 assert(s_chm_status.post_failures==1&&s_chm_timers[1].identity);
 assert(!esp32qjs_wifi_chm_timer_service_native()&&native_ends==2);
 /* Failed wake admission remains serviceable through the native poll. */
 now+=100;post_error=-1;wake(1);post_error=0;
 assert(!esp32qjs_wifi_chm_timer_service_native()&&native_ends==3);
 assert(s_chm_status.post_failures==2);
 rearm_max(100);now+=100;wake(1);
 esp32qjs_wifi_chm_record_reset(g_chm+4,0,CHM_RECORD_SIZE);g_chm[4]=255;
 deliver(messages_count-1);assert(native_ends==3);
 uint32_t last=s_chm_status.last_identity;
 esp_timer_create_args_t late=handles[timer(1)->timer_arg].args;
 esp32qjs_wifi_chm_timer_done(timer(0));esp32qjs_wifi_chm_timer_done(timer(1));
 g_chm=NULL;late.callback(late.arg);assert(native_ends==3);
 g_chm=storage;esp32qjs_wifi_chm_timer_setfn(timer(0),original_callback,NULL);
 esp32qjs_wifi_chm_timer_setfn(timer(1),original_callback,(void*)1);
 rearm_max(100);late.callback(late.arg);assert(s_chm_status.last_identity>last&&native_ends==3);

 initialize();rearm_max(100);delete_error=77;
 esp_timer_handle_t kept=timer(1)->timer_arg;
 assert(esp32qjs_wifi_chm_timer_done(timer(1))&&timer(1)->timer_arg==kept);
 g_chm=NULL;esp32qjs_wifi_chm_timer_status_t status;esp32qjs_wifi_chm_timer_status(&status);
 assert(status.error==77&&status.cleanup_error==77&&status.timers_held==2&&!status.active_arms);
 g_chm=storage;delete_error=0;esp32qjs_wifi_chm_timer_done(timer(1));
 assert(!timer(1)->timer_arg&&!s_chm_timers[1].handle);
 assert(__wrap_chm_start_op(NULL,0,10,NULL,NULL,NULL)==3&&!native_starts);
 assert(esp32qjs_wifi_chm_timer_service_native()==77); /* no claimed recovery */

 initialize();start_error=66;rearm_max(1);
 assert(s_chm_status.error==66&&!s_chm_timers[1].identity);
 initialize();stop_error=65;rearm_max(1);
 assert(s_chm_status.error==65&&!starts);
 initialize();esp32qjs_wifi_chm_timer_done(timer(1));create_error=64;
 esp32qjs_wifi_chm_timer_setfn(timer(1),original_callback,(void*)1);
 assert(s_chm_status.error==64&&!s_chm_timers[1].handle);
 initialize();s_chm_status.last_identity=UINT32_MAX-1;
 assert(__wrap_chm_start_op(NULL,0,10,NULL,NULL,NULL)==3&&!native_starts);
 assert(s_chm_status.error==ESP_ERR_NO_MEM&&s_chm_status.last_identity==UINT32_MAX-1);
 initialize();esp32qjs_wifi_chm_timer_arm(timer(1),UINT64_MAX,false);
 assert(s_chm_status.error==ESP_ERR_INVALID_ARG&&!starts);
 initialize();esp32qjs_wifi_chm_timer_arm(timer(1),100,true);
 assert(s_chm_status.error==ESP_ERR_INVALID_ARG&&!starts);
 assert(!critical);return 0;
}
'''
