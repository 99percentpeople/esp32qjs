"""Deferred real EventQueue allocators/factory/receive retention + memory manager.

The existing manager fixture supplies heap/lock boundaries. JS and RTOS creation
are injected; production queue/resource/free/receive functions and the complete
manager execute together. This does not prove ESP32 RTOS ABI or full VM behavior.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from wireless_vm_fixture import ROOT, CORE, extract


class EventQueueBudget(unittest.TestCase):
    def test_allocation_failure_control_reserve_native_retention_and_rtos_delete(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (CORE / 'esp32_mquickjs_event_queue.c').read_text()
        heap = (ROOT / 'tests/c/test_memory_wireless.c').read_text().split('int main(void)')[0]
        start = source.index('struct esp32_mquickjs_event_queue {')
        end = source.index('static void *event_queue_allocate')
        structs = source[start:end]
        code = heap + SHIM + structs + BOUNDARIES
        for name in ['event_queue_allocate', 'event_queue_resource_allocate', 'event_queue_resource_release',
                     'event_queue_resource_create_lock', 'event_queue_resource_delete_lock',
                     'event_queue_resource_create_queue', 'event_queue_resource_delete_queue']:
            code += extract(source, name)
        start = source.index('static const esp32_mquickjs_event_queue_resource_ops_t')
        code += source[start:source.index('\n    };', start) + len('\n    };')]
        for name in ['event_queue_runtime', 'event_queue_register', 'event_queue_destroy_native',
                     'event_queue_take_destroy_ownership_locked', 'esp32_mquickjs_event_queue_retain',
                     'esp32_mquickjs_event_queue_release', 'event_queue_future_prepare',
                     'event_queue_future_destroy', 'event_queue_new', 'esp32_mquickjs_event_queue_new',
                     'esp32_mquickjs_event_queue_new_wireless']:
            code += extract(source, name)
        code += 'typedef struct {const char *memory_owner;} future_slot_t;\n'
        code += extract((CORE / 'esp32_mquickjs_future.c').read_text(), 'future_control_allocate')
        with tempfile.TemporaryDirectory() as temp:
            p, binary = Path(temp) / 'case.c', Path(temp) / 'case'
            p.write_text(code + MAIN)
            sources = [CORE / ('esp32_mquickjs_' + n + '.c') for n in
                       ['memory', 'memory_budget', 'memory_owner_accounting', 'memory_dma_accounting', 'event_queue_resources']]
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', '-I' + str(ROOT / 'tests/c/memory_stubs'),
                                    '-I' + str(ROOT / 'components/esp32_mquickjs/internal'), str(p),
                                    *map(str, sources), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


SHIM = r'''
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "esp32_mquickjs_event_queue_resources.h"
#define portENTER_CRITICAL taskENTER_CRITICAL
#define portEXIT_CRITICAL taskEXIT_CRITICAL
#define JS_CLASS_EVENT_QUEUE 1
#define JS_EXCEPTION (-1)
typedef int JSContext;
typedef intptr_t JSValue;
typedef struct {JSValue val;} JSGCRef;
typedef struct {void *event_queue_state;} esp32_mquickjs_runtime_t;
typedef struct {uint8_t slot;uint32_t generation;} esp32_mquickjs_future_token_t;
typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
typedef JSValue (*esp32_mquickjs_event_queue_to_js_fn)(JSContext *,const void *,void *);
typedef void (*esp32_mquickjs_event_queue_drop_fn)(void *,void *);
typedef void (*esp32_mquickjs_event_queue_close_fn)(void *);
typedef enum {ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST} esp32_mquickjs_event_queue_overflow_t;
typedef uint32_t UBaseType_t;
typedef struct {bool dynamic;uint32_t magic,capacity,item_size;uint8_t *bytes;} StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t;
typedef StaticQueue_t *QueueHandle_t,*SemaphoreHandle_t;
typedef void *esp_timer_handle_t;
'''
BOUNDARIES = r'''
static esp32_mquickjs_event_queue_t *object_queue;
static bool fail_js,fail_mutex,fail_queue;
static unsigned roots,deletes;
#define JS_IsException(v) ((v)==JS_EXCEPTION)
static JSValue throw_error(JSContext *ctx,...) {(void)ctx;return JS_EXCEPTION;}
#define JS_ThrowInternalError throw_error
#define JS_ThrowRangeError throw_error
#define JS_ThrowTypeError throw_error
#define JS_ThrowOutOfMemory throw_error
static JSValue JS_NewObjectClassUser(JSContext *ctx,int id){(void)ctx;assert(id==1);return fail_js ? JS_EXCEPTION : 1;}
static void JS_SetOpaque(JSContext *ctx,JSValue object,void *p){(void)ctx;assert(object==1);object_queue=p;}
static JSValue *JS_AddGCRef(JSContext *ctx,JSGCRef *r){(void)ctx;++roots;return &r->val;}
static void JS_DeleteGCRef(JSContext *ctx,JSGCRef *r){(void)ctx;(void)r;assert(roots);--roots;}
static esp32_mquickjs_event_queue_t *event_queue_from_value(JSContext *ctx,JSValue object){(void)ctx;return object==1 ? object_queue : NULL;}
static bool esp32_mquickjs_value_to_bounded_u32(JSContext *ctx,JSValue v,uint32_t a,uint32_t b,uint32_t *out){(void)ctx;assert(v>=(intptr_t)a && v<=(intptr_t)b);*out=(uint32_t)v;return true;}
static int esp_timer_stop(esp_timer_handle_t p){(void)p;abort();}
static int esp_timer_delete(esp_timer_handle_t p){(void)p;abort();}
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p){assert(!test_memory_lock_depth);if(fail_mutex)return NULL;p->magic=123;return p;}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){StaticSemaphore_t *p=heap_caps_calloc(1,sizeof(*p),MALLOC_CAP_8BIT);if(p){p->dynamic=true;p->magic=123;}return p;}
static QueueHandle_t xQueueCreateStatic(uint32_t n,UBaseType_t size,uint8_t *bytes,StaticQueue_t *p){assert(!test_memory_lock_depth);if(fail_queue)return NULL;p->capacity=n;p->item_size=size;p->bytes=bytes;p->magic=123;return p;}
static QueueHandle_t xQueueCreate(uint32_t n,size_t size){StaticQueue_t *p=heap_caps_calloc(1,sizeof(*p)+n*size,MALLOC_CAP_8BIT);if(p){p->dynamic=true;p->magic=123;}return p;}
static void vQueueDelete(QueueHandle_t p){assert(!test_memory_lock_depth && p->magic==123);++deletes;if(p->dynamic)heap_caps_free(p);else {assert(status().wireless.regions[0].reserved>0);p->magic=0;}}
#define vSemaphoreDelete vQueueDelete
static JSValue convert(JSContext *ctx,const void *p,void *opaque){(void)ctx;(void)p;(void)opaque;return 0;}
'''
MAIN = r'''
int main(void){
    JSContext ctx=0;esp32_mquickjs_event_queue_runtime_t registry={0};
    esp32_mquickjs_runtime_t runtime={.event_queue_state=&registry};
    for(unsigned nth=1;nth<=10;++nth){
        fail_at=nth;allocation_calls=0;
        assert(esp32_mquickjs_event_queue_new_wireless("wifi",&ctx,&runtime,16,4,
            ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,convert,NULL,NULL,NULL)==JS_EXCEPTION);
        empty();assert(!registry.head);
    }
    fail_at=0;
    bool *failures[]={&fail_js,&fail_mutex,&fail_queue};
    for(size_t i=0;i<3;++i){
        *failures[i]=true;
        assert(esp32_mquickjs_event_queue_new_wireless("wifi",&ctx,&runtime,16,4,
            ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,convert,NULL,NULL,NULL)==JS_EXCEPTION);
        *failures[i]=false;empty();assert(!registry.head);
    }
    unsigned before=allocation_calls;
    assert(esp32_mquickjs_event_queue_new_wireless("wifi",&ctx,&runtime,SIZE_MAX,2,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,convert,NULL,NULL,NULL)==JS_EXCEPTION);
    assert(before==allocation_calls);
    assert(esp32_mquickjs_event_queue_new_wireless("wifi",&ctx,&runtime,16,4,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,convert,NULL,NULL,NULL)==1);
    esp32_mquickjs_event_queue_t *q=object_queue;
    size_t initial=status().wireless.regions[0].reserved;
    void *probe=esp32_mquickjs_memory_wireless_alloc("fill",1,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    assert(probe);size_t overhead=status().wireless.regions[0].reserved-initial-1;
    esp32_mquickjs_memory_payload_free(probe);
    void *fill=esp32_mquickjs_memory_wireless_alloc("fill",3584-initial-overhead,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(fill);
    future_slot_t slot={.memory_owner="wireless.future"};
    void *handle=future_control_allocate(&slot,1,16);assert(handle);
    esp32_mquickjs_memory_payload_free(handle);
    JSGCRef receiver={.val=1};esp32_mquickjs_future_driver_state_t *pending=NULL;
    assert(event_queue_future_prepare(&ctx,&receiver,0,NULL,&pending));
    assert(pending && roots==1 && q->native_retain_count==1);
    assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL]>0);
    /* The real last native retain destroys a disposed queue only after receive storage is released. */
    q->dispose_requested=true;atomic_store(&q->closed,true);registry.head=NULL;
    event_queue_future_destroy(pending);
    assert(!roots && status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_QUEUE]==0);
    esp32_mquickjs_memory_payload_free(fill);empty();
    assert(esp32_mquickjs_event_queue_new(&ctx,&runtime,16,4,ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,convert,NULL,NULL,NULL)==1);
    assert(status().wireless.regions[0].reserved==0);registry.head=NULL;
    event_queue_destroy_native(object_queue);empty();assert(deletes>0);
    return 0;
}
'''
