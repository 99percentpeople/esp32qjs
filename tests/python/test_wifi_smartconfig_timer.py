"""Deferred production SmartConfig timer ownership and SDK dispatch tests."""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]


class SmartConfigTimers(unittest.TestCase):
    def test_production_timer_registry_and_wrappers(self):
        paths = [
            'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_smartconfig_timer.h',
            'components/esp32_mquickjs/src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_timer.c',
            'components/esp32_mquickjs/src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_timer_sdk.c',
        ]
        production = ''.join(re.sub(r'^#(?:include|pragma)[^\n]*\n', '', (ROOT / p).read_text(), flags=re.M) for p in paths)
        compile_run(self, BOUNDARIES + production + CASES)


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 0
#define CONFIG_IDF_TARGET_ESP32C5 0
#define IRAM_ATTR
#define DRAM_ATTR
#define portMUX_INITIALIZER_UNLOCKED 0
#define ESP_TIMER_TASK 0
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
typedef int esp_err_t,portMUX_TYPE;
typedef void ETSTimerFunc(void *);
typedef struct { void *timer_arg;uint32_t timer_expire; } ETSTimer;
typedef struct {uint64_t identity;uint32_t radio_generation;} esp32_mquickjs_wifi_smartconfig_token_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
typedef struct timer {esp_timer_create_args_t args;bool alive,armed;} *esp_timer_handle_t;
static struct timer timer_storage[64];
static int locked,allocs,frees,creates,starts,stops,deletes,forwards;
static int create_error,start_error,stop_error,blocking_error,delete_error;
static bool alloc_error;
static uint64_t latest_us;
static bool latest_repeat;
static void (*during_cleanup)(void);
#define portENTER_CRITICAL_SAFE(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL_SAFE(p) do{(void)(p);assert(locked);locked=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(!locked && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(alloc_error)return NULL;
    allocs++;return calloc(n,size);
}
static void heap_caps_free(void *p){assert(!locked && p);frees++;free(p);}
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(!locked && args->callback && args->arg && args->dispatch_method==ESP_TIMER_TASK);
    if(create_error)return create_error;assert(creates<64);
    *out=&timer_storage[creates++];(*out)->args=*args;(*out)->alive=true;return ESP_OK;
}
static esp_err_t esp_timer_stop(esp_timer_handle_t t) {
    assert(!locked && t && t->alive);stops++;
    if(stop_error)return stop_error;bool was=t->armed;t->armed=false;return was?ESP_OK:ESP_ERR_INVALID_STATE;
}
static esp_err_t esp_timer_stop_blocking(esp_timer_handle_t t,unsigned ticks) {
    assert(!locked && t && t->alive && ticks==1);stops++;
    if(during_cleanup)during_cleanup();
    if(blocking_error)return blocking_error;t->armed=false;return ESP_OK;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t t) {
    assert(!locked && t && t->alive);deletes++;
    if(delete_error)return delete_error;assert(!t->armed);t->alive=false;return ESP_OK;
}
static esp_err_t start(esp_timer_handle_t t,uint64_t us,bool repeat) {
    assert(!locked && t && t->alive && !t->armed);starts++;latest_us=us;latest_repeat=repeat;
    if(start_error)return start_error;t->armed=true;return ESP_OK;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t t,uint64_t us){return start(t,us,false);}
static esp_err_t esp_timer_start_periodic(esp_timer_handle_t t,uint64_t us){return start(t,us,true);}
void __real_ets_timer_setfn(ETSTimer *t,ETSTimerFunc *fn,void *arg){assert(!locked && t && fn);(void)arg;forwards++;}
void __real_ets_timer_disarm(ETSTimer *t){assert(!locked && t);forwards++;}
void __real_ets_timer_done(ETSTimer *t){assert(!locked && t);forwards++;}
void __real_ets_timer_arm_us(ETSTimer *t,uint32_t us,bool repeat){assert(!locked && t && us);(void)repeat;forwards++;}
void __real_ets_timer_arm(ETSTimer *t,uint32_t ms,bool repeat){assert(!locked && t && ms);(void)repeat;forwards++;}
'''

CASES = r'''
static ETSTimer native[ESP32_MQUICKJS_SMARTCONFIG_TIMERS],foreign;
static ETSTimer *addresses[ESP32_MQUICKJS_SMARTCONFIG_TIMERS];
static esp32_mquickjs_wifi_smartconfig_token_t owner={.identity=1,.radio_generation=7};
static int called;
static bool close_inside_callback;
static void native_callback(void *arg) {
    assert(arg==&called && !locked);called++;
    if(close_inside_callback){
        assert(esp32_mquickjs_wifi_smartconfig_timers_close(&owner)==ESP_OK);
        __wrap_ets_timer_done(&native[0]);
        assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_ERR_INVALID_STATE);
    }
}
static void release_during_cleanup(void) {
    assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_ERR_INVALID_STATE);
}
static void clear_owner(void) {
    assert(esp32_mquickjs_wifi_smartconfig_timers_close(&owner)==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_timers_cleanup(&owner)==ESP_OK);
    assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_OK);
    assert(allocs==frees);owner.identity++;
}
int main(void) {
    for(unsigned i=0;i<ESP32_MQUICKJS_SMARTCONFIG_TIMERS;i++)addresses[i]=&native[i];
    __wrap_ets_timer_setfn(&foreign,native_callback,&called);__wrap_ets_timer_disarm(&foreign);
    __wrap_ets_timer_done(&foreign);__wrap_ets_timer_arm(&foreign,2,false);__wrap_ets_timer_arm_us(&foreign,3,true);
    assert(forwards==5);
    alloc_error=true;assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_ERR_NO_MEM);
    alloc_error=false;
    addresses[1]=addresses[0];assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_ERR_INVALID_ARG);
    addresses[1]=&native[1];native[2].timer_arg=&foreign;
    assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_ERR_INVALID_STATE);
    native[2].timer_arg=NULL;
    assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_OK);
    esp32_mquickjs_wifi_smartconfig_token_t stale=owner;stale.radio_generation++;
    assert(esp32_mquickjs_wifi_smartconfig_timers_close(&stale)==ESP_ERR_INVALID_STATE);
    create_error=ESP_ERR_NO_MEM;__wrap_ets_timer_setfn(&native[0],native_callback,&called);
    assert(!native[0].timer_arg && s_sc_timers->error==ESP_ERR_NO_MEM);
    create_error=0;__wrap_ets_timer_setfn(&native[0],native_callback,&called);
    esp_timer_handle_t first=native[0].timer_arg;assert(first);
    void *old_arg=first->args.arg;
    __wrap_ets_timer_arm(&native[0],UINT32_MAX,true);
    assert(latest_us==UINT64_C(4294967295000) && latest_repeat);
    first->args.callback(first->args.arg);assert(called==1);
    __wrap_ets_timer_disarm(&native[0]);first->args.callback(first->args.arg);assert(called==1);
    start_error=ESP_FAIL;__wrap_ets_timer_arm_us(&native[0],20,false);assert(!first->armed);
    start_error=0;__wrap_ets_timer_arm_us(&native[0],20,false);assert(first->armed);
    blocking_error=ESP_ERR_TIMEOUT;__wrap_ets_timer_done(&native[0]);
    assert(native[0].timer_arg==first && first->alive && !s_sc_timers->entries[0].enabled);
    blocking_error=0;delete_error=ESP_FAIL;__wrap_ets_timer_done(&native[0]);
    assert(native[0].timer_arg==first && first->alive);
    int stopped_count=stops,started_count=starts;
    __wrap_ets_timer_arm(&native[0],1,false);assert(starts==started_count);
    __wrap_ets_timer_disarm(&native[0]);assert(stops==stopped_count);
    delete_error=0;__wrap_ets_timer_done(&native[0]);assert(!native[0].timer_arg && !first->alive && stops==stopped_count);
    __wrap_ets_timer_setfn(&native[0],native_callback,&called);
    esp_timer_handle_t second=native[0].timer_arg;assert(second && second->args.arg!=old_arg);
    __wrap_ets_timer_arm(&native[0],1,false);
    first->args.callback(old_arg);assert(called==1);
    close_inside_callback=true;second->args.callback(second->args.arg);assert(called==2);
    assert(!second->alive && !native[0].timer_arg);
    __wrap_ets_timer_setfn(&native[0],native_callback,&called);assert(!native[0].timer_arg);
    clear_owner();close_inside_callback=false;
    assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_OK);
    for(unsigned i=0;i<ESP32_MQUICKJS_SMARTCONFIG_TIMERS;i++){
        __wrap_ets_timer_setfn(&native[i],native_callback,&called);__wrap_ets_timer_arm(&native[i],1,false);
    }
    assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_smartconfig_timers_close(&owner)==ESP_OK);
    delete_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_timers_cleanup(&owner)==ESP_FAIL);
    assert(esp32_mquickjs_wifi_smartconfig_timers_release(&owner)==ESP_ERR_INVALID_STATE);
    delete_error=0;during_cleanup=release_during_cleanup;
    assert(esp32_mquickjs_wifi_smartconfig_timers_cleanup(&owner)==ESP_OK);
    during_cleanup=NULL;
    esp32_mquickjs_wifi_smartconfig_timer_status_t status;
    assert(esp32_mquickjs_wifi_smartconfig_timers_status(&owner,&status)==ESP_OK);
    assert(!status.handles && !status.callbacks && !status.busy && !status.cleanup_error && status.error==ESP_FAIL);
    clear_owner();
    assert(esp32_mquickjs_wifi_smartconfig_timers_begin(&owner,addresses)==ESP_OK);
    s_sc_timer_identity=UINT32_MAX;
    __wrap_ets_timer_setfn(&native[0],native_callback,&called);
    assert(!native[0].timer_arg && s_sc_timers->error==ESP_ERR_NO_MEM);
    clear_owner();assert(forwards==5);
    return 0;
}
'''
