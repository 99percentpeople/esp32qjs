"""Deferred production registry and deadline-result regressions; AST only now."""
import re
import unittest
from pathlib import Path
from test_wireless_control_regression import compile_run


SOURCE = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs/src/core/esp32_mquickjs_future.c'


def extract(text, name):
    match = re.search(r'^[^\n;{}]*\b' + name + r'\s*\([^;{}]*\)\s*\{', text, re.M)
    if not match:
        raise AssertionError(name)
    depth, end = 1, match.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'


def registry_source():
    text = SOURCE.read_text()
    constants = '\n'.join(re.findall(r'^#define ESP32_MQUICKJS_FUTURE_(?:MAX_DRIVERS|DRIVER_CHUNK_SIZE) .*$', text, re.M))
    entry = re.search(r'typedef struct \{\s*JSGCRef function;.*?\} future_driver_entry_t;', text, re.S).group(0)
    chunk = re.search(r'typedef struct future_driver_chunk \{.*?\} future_driver_chunk_t;', text, re.S).group(0)
    return (COMMON + constants + '\n' + entry + chunk + REGISTRY_BOUNDARIES +
            ''.join(extract(text, name) for name in ('future_find_driver', 'future_clear_driver_registry',
                'esp32_mquickjs_future_register_driver')))


class FutureDriverRegistry(unittest.TestCase):
    def test_more_than_128_methods_keep_existing_roots_at_stable_addresses(self):
        compile_run(self, registry_source() + r'''
int main(void){
 JSContext ctx=0;future_runtime_t state={0};esp32_mquickjs_runtime_t runtime={.state=&state};
 esp32_mquickjs_future_driver_t driver={0},different={1};
 for(JSValue i=1;i<=16;++i)assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,i,&driver));
 JSGCRef*first=roots[0];assert(live==1&&first->val==1);
 for(JSValue i=17;i<=154;++i)assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,i,&driver));
 assert(state.driver_count==154&&root_count==154&&live==10&&roots[0]==first&&first->val==1);
 assert(future_find_driver(&state,154)==&driver);
 assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,1,&driver));
 assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,1,&different)&&state.driver_count==154);
 future_clear_driver_registry(&ctx,&state);assert(!roots[0]&&!root_count&&!live&&!state.drivers&&!state.driver_count);
}
''')

    def test_chunk_oom_preserves_registered_methods_and_retries_only_allocation(self):
        compile_run(self, registry_source() + r'''
int main(void){
 JSContext ctx=0;future_runtime_t state={0};esp32_mquickjs_runtime_t runtime={.state=&state};esp32_mquickjs_future_driver_t driver={0};
 fail_allocation=1;assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,1,&driver));
 assert(!state.driver_count&&!state.drivers&&!live&&!root_count&&ctx);
 ctx=0;fail_allocation=0;
 for(JSValue i=1;i<=16;++i)assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,i,&driver));
 JSGCRef*first=roots[0];fail_allocation=allocations+1;
 assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,17,&driver));
 assert(state.driver_count==16&&live==1&&root_count==16&&roots[0]==first&&future_find_driver(&state,1)==&driver);
 ctx=0;fail_allocation=0;assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,17,&driver));
 assert(state.driver_count==17&&live==2);future_clear_driver_registry(&ctx,&state);assert(!root_count&&!live);
}
''')

    def test_exact_capacity_and_shutdown_reject_new_entries_without_allocation(self):
        compile_run(self, registry_source() + r'''
int main(void){
 JSContext ctx=0;future_runtime_t state={0};esp32_mquickjs_runtime_t runtime={.state=&state};esp32_mquickjs_future_driver_t driver={0};
 for(JSValue i=1;i<=ESP32_MQUICKJS_FUTURE_MAX_DRIVERS;++i)assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,i,&driver));
 unsigned before=allocations;assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,1000,&driver));
 assert(allocations==before&&state.driver_count==ESP32_MQUICKJS_FUTURE_MAX_DRIVERS);
 future_clear_driver_registry(&ctx,&state);state.shutting_down=true;
 assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,1,&driver)&&allocations==before&&!root_count&&!live);
}
''')


class FutureDeadlineResult(unittest.TestCase):
    def source(self):
        return COMMON + DEADLINE_BOUNDARIES + extract(SOURCE.read_text(), 'future_expire_deadlines')

    def test_empty_receive_timeout_fulfils_null_and_preserves_native_cancellation(self):
        compile_run(self, self.source() + r'''
int main(void){
 JSContext ctx=0;esp32_mquickjs_runtime_t runtime={0};setup();callback_result=JS_NULL;
 assert(future_expire_deadlines(&ctx,&runtime));
 assert(slot.state==FUTURE_STATE_FULFILLED&&settled_value==JS_NULL&&cancelled==1&&!generic_errors&&!root_count);
}
''')

    def test_custom_success_and_error_are_rooted_during_cancellation(self):
        compile_run(self, self.source() + r'''
int main(void){
 JSContext ctx=0;esp32_mquickjs_runtime_t runtime={0};setup();callback_result=100;
 assert(future_expire_deadlines(&ctx,&runtime));assert(settled_value==200&&slot.state==FUTURE_STATE_FULFILLED);
 setup();callback_result=JS_EXCEPTION;pending_exception=100;
 assert(future_expire_deadlines(&ctx,&runtime));assert(settled_value==200&&slot.state==FUTURE_STATE_REJECTED&&!root_count);
 setup();driver.on_timeout=NULL;assert(future_expire_deadlines(&ctx,&runtime));
 assert(slot.state==FUTURE_STATE_REJECTED&&generic_errors==1&&!root_count);
}
''')


COMMON = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
typedef intptr_t JSValue;typedef int JSContext;
typedef struct{JSValue val;}JSGCRef;
typedef struct{int value;}esp32_mquickjs_future_driver_t;
#define JS_NULL (-2)
#define JS_UNDEFINED (-3)
#define JS_EXCEPTION (-1)
static JSGCRef*roots[300];static unsigned root_count;
static JSValue*JS_AddGCRef(JSContext*ctx,JSGCRef*ref){(void)ctx;assert(root_count<300);roots[root_count++]=ref;return &ref->val;}
static void JS_DeleteGCRef(JSContext*ctx,JSGCRef*ref){(void)ctx;unsigned i=0;while(i<root_count&&roots[i]!=ref)++i;
 assert(i<root_count);roots[i]=roots[--root_count];roots[root_count]=NULL;}
static JSValue*JS_PushGCRef(JSContext*ctx,JSGCRef*ref){ref->val=JS_UNDEFINED;return JS_AddGCRef(ctx,ref);}
static JSValue JS_PopGCRef(JSContext*ctx,JSGCRef*ref){JSValue value=ref->val;JS_DeleteGCRef(ctx,ref);return value;}
'''

REGISTRY_BOUNDARIES = r'''
typedef struct{future_driver_chunk_t*drivers;size_t driver_count;bool shutting_down;}future_runtime_t;
typedef struct{future_runtime_t*state;}esp32_mquickjs_runtime_t;
static unsigned allocations,fail_allocation,live;
static future_runtime_t*future_runtime(esp32_mquickjs_runtime_t*r){return r?r->state:NULL;}
static bool JS_IsFunction(JSContext*ctx,JSValue value){(void)ctx;return value>0;}
static void JS_ThrowOutOfMemory(JSContext*ctx){*ctx=1;}
static void*future_runtime_resource_allocate(size_t n,size_t bytes,void*opaque){
 (void)opaque;if(++allocations==fail_allocation)return NULL;void*p=calloc(n,bytes);assert(p);++live;return p;}
static void future_runtime_resource_release(void*p,void*opaque){(void)opaque;
 for(unsigned i=0;i<root_count;++i)assert((uintptr_t)roots[i]<(uintptr_t)p||(uintptr_t)roots[i]>=(uintptr_t)p+sizeof(future_driver_chunk_t));
 assert(live);--live;free(p);}
#define ESP_LOGE(...) ((void)0)
'''

DEADLINE_BOUNDARIES = r'''
#define ESP32_MQUICKJS_FUTURE_SLOT_COUNT 1
typedef int esp32_mquickjs_cancel_result_t;
enum{ESP32_MQUICKJS_CANCELLED,ESP32_MQUICKJS_CANCEL_REQUESTED};
enum{FUTURE_STATE_QUEUED,FUTURE_STATE_PENDING,FUTURE_STATE_FULFILLED,FUTURE_STATE_REJECTED};
enum{FUTURE_KIND_DRIVER,FUTURE_KIND_SLEEP,FUTURE_KIND_TIMEOUT};
typedef struct{JSValue(*on_timeout)(JSContext*,void*,uint32_t);esp32_mquickjs_cancel_result_t(*cancel)(void*);}deadline_driver_t;
typedef struct{bool allocated,driver_active,cancel_requested;int state,kind;uint64_t submitted_us,deadline_us;
 deadline_driver_t*driver;void*driver_state;}future_slot_t;
typedef struct{future_slot_t*slots;}future_runtime_t;
typedef struct{int unused;}esp32_mquickjs_runtime_t;
static future_slot_t slot;static future_runtime_t state={.slots=&slot};static deadline_driver_t driver;
static unsigned cancelled,generic_errors;static JSValue callback_result,pending_exception,settled_value;
static future_runtime_t*future_runtime(esp32_mquickjs_runtime_t*r){(void)r;return &state;}
static int64_t esp_timer_get_time(void){return 100000;}
static bool future_is_terminal(int value){return value>=FUTURE_STATE_FULFILLED;}
static uint32_t esp32_mquickjs_future_elapsed_timeout_ms(uint64_t a,uint64_t b){return (b-a)/1000;}
static bool JS_IsException(JSValue value){return value==JS_EXCEPTION;}
static bool JS_HasException(JSContext*ctx){(void)ctx;return pending_exception!=0;}
static JSValue JS_GetException(JSContext*ctx){(void)ctx;JSValue v=pending_exception;pending_exception=0;return v;}
static void future_destroy_driver(future_slot_t*s){s->driver_active=false;}
static void future_settle(JSContext*ctx,future_slot_t*s,int terminal,JSValue value){(void)ctx;s->state=terminal;settled_value=value;}
static void future_reject_message(JSContext*ctx,future_slot_t*s,const char*message){(void)ctx;assert(strstr(message,"timed out"));++generic_errors;s->state=FUTURE_STATE_REJECTED;}
static JSValue timeout_callback(JSContext*ctx,void*p,uint32_t ms){(void)ctx;(void)p;assert(ms==10);return callback_result;}
static esp32_mquickjs_cancel_result_t cancel_callback(void*p){(void)p;assert(root_count==1);++cancelled;
 if(roots[0]->val==100)roots[0]->val=200;return ESP32_MQUICKJS_CANCELLED;}
static void setup(void){assert(!root_count);cancelled=generic_errors=0;pending_exception=0;settled_value=0;
 driver=(deadline_driver_t){timeout_callback,cancel_callback};slot=(future_slot_t){.allocated=true,.driver_active=true,
 .state=FUTURE_STATE_PENDING,.kind=FUTURE_KIND_DRIVER,.submitted_us=1000,.deadline_us=11000,.driver=&driver};}
'''
