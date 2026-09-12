"""Exercise production incoming-connection queue registration and drop paths."""
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).parent))
from test_wireless_control_regression import function,compile_run
BLE=pathlib.Path(__file__).resolve().parents[2]/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
FIXTURE=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#define portMAX_DELAY 0
static int s_ble_connection_mutex,connection_lock_depth;
#define xSemaphoreTakeRecursive(m,t) ((void)(m),(void)(t),connection_lock_depth++)
#define xSemaphoreGiveRecursive(m) ((void)(m),assert(connection_lock_depth>0),connection_lock_depth--)
#define BLE_HS_ENOTCONN 2
#define BLE_ERR_REM_USER_CONN_TERM 3
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
#define ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST 0
#define BLE_STOP_CONNECTED 1
typedef struct { unsigned generation,connection_index,connection_generation,sequence; int64_t timestamp_us; } ble_advertiser_event_t;
typedef struct { unsigned generation; int queue_ref,stop_reason; bool queue_rooted,active; atomic_uint sequence,incoming,dropped; void *queue; } ble_advertiser_t;
typedef struct { bool open,allocated,reserved,release_on_disconnect; atomic_bool incoming_pending; unsigned generation; atomic_uint incoming_advertiser_generation; int conn_handle,connect_operation; } ble_connection_slot_t;
typedef struct { int lock; bool host_started; void *ctx,*runtime; unsigned max_connections; ble_advertiser_t advertiser; ble_connection_slot_t connections[3]; atomic_int rejected_connection_error; } ble_adapter_t;
static ble_adapter_t s_ble={.max_connections=3,.host_started=true};
static int terminated,terminate_result,quiesced;
static int last_handle,lock_depth;
#define taskENTER_CRITICAL(lock) do { (void)(lock);assert(lock_depth++==0); } while(0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock);assert(--lock_depth==0); } while(0)
static void (*registered_drop)(void *,void *);
static void *registered_opaque;
static bool queue_full;
static ble_advertiser_event_t queued;
static int64_t esp_timer_get_time(void) { return 123; }
static bool esp32_mquickjs_event_queue_try_send_from_callback(void *q,const void *e) {
    (void)q;queued=*(const ble_advertiser_event_t *)e;return !queue_full;
}
static int ble_gap_terminate(int h,int reason) { (void)reason;assert(!lock_depth);terminated++;last_handle=h;return terminate_result; }
static bool esp32_mquickjs_wireless_native_operation_complete(int *s) { *s=0;return true; }
static void ble_advertise_callbacks_quiesce(void) { quiesced++; }
static void ble_release_event_queue(void *ctx,void **q,int *r,bool *rooted) { (void)ctx;(void)q;(void)r;(void)rooted;assert(quiesced); }
static void ble_advertiser_event_to_js(void) {}
static void *esp32_mquickjs_event_queue_new_wireless(const char *owner,void *ctx,void *runtime,size_t size,unsigned capacity,int overflow,void (*convert)(void),void (*drop)(void *,void *),void *unused,void *opaque) {
    (void)owner;(void)ctx;(void)runtime;(void)size;(void)capacity;(void)overflow;(void)convert;(void)unused;
    registered_drop=drop;registered_opaque=opaque;return (void *)1;
}
'''
class BleIncomingQueueRegression(unittest.TestCase):
    def production(self):
        source=BLE.read_text()
        return "\n".join(function(source,name) for name in (
            "ble_terminate_unclaimed_connection","ble_advertiser_event_drop",
            "ble_publish_incoming_connection",
            "ble_discard_advertiser_connections","ble_release_advertiser"))

    def factory(self):
        source=BLE.read_text()
        capture=function(source,'ble_advertise_capture')
        start=capture.index('    queue = esp32_mquickjs_event_queue_new_wireless(')
        call=capture[start:capture.index(';',start)+1]
        return 'static void create(void) { void *ctx=NULL,*queue;unsigned capacity=2;ble_adapter_t *adapter=&s_ble;ble_advertiser_t *advertiser=&s_ble.advertiser;'+call+' (void)queue; }\n'

    def test_production_queue_registers_native_owner_drop(self):
        compile_run(self,FIXTURE+self.production()+self.factory()+r'''
int main(void) { create();assert(registered_drop!=NULL && registered_opaque==&s_ble.advertiser); }
''')

    def test_drop_is_idempotent_and_preserves_other_generations(self):
        compile_run(self,FIXTURE+self.production()+self.factory()+r'''
int main(void) {
    create();
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,.conn_handle=12};
    ble_advertiser_event_t event={.generation=4,.connection_generation=6};
    registered_drop(&event,registered_opaque);
    assert(!terminated && s_ble.connections[0].incoming_pending);
    event.connection_generation=7;event.generation=3;
    registered_drop(&event,registered_opaque);
    assert(!terminated && s_ble.connections[0].incoming_pending);
    event.generation=4;terminate_result=17;
    registered_drop(&event,registered_opaque);
    assert(terminated==1 && last_handle==11 && s_ble.rejected_connection_error==17);
    assert(s_ble.connections[0].open && s_ble.connections[0].allocated);
    assert(s_ble.connections[0].release_on_disconnect && !s_ble.connections[0].incoming_pending);
    registered_drop(&event,registered_opaque);
    assert(terminated==1 && s_ble.connections[1].open && !s_ble.connections[1].release_on_disconnect);
}
''')

    def test_close_claims_popped_receive_event_but_not_transferred_connections(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    s_ble.advertiser.generation=4;
    /* Slot 0's event has already left the queue for a receive Future. */
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,
        .generation=8,.incoming_advertiser_generation=4,.conn_handle=12};
    s_ble.connections[2]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=9,.incoming_advertiser_generation=5,.conn_handle=13};
    ble_advertiser_event_t event={.generation=4,.connection_generation=7};
    ble_release_advertiser(&s_ble);
    assert(terminated==1 && last_handle==11 && quiesced==1);
    ble_advertiser_event_drop(&event,&s_ble.advertiser);
    assert(terminated==1 && s_ble.connections[1].open && s_ble.connections[2].incoming_pending);
}
''')

    def test_closed_connection_and_stopped_host_need_no_native_mutation(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    ble_advertiser_event_t event={.generation=4,.connection_generation=7};
    ble_advertiser_event_drop(&event,NULL);
    assert(!terminated && !s_ble.connections[0].allocated);
    s_ble.connections[0].open=true;s_ble.connections[0].allocated=true;
    s_ble.connections[0].incoming_pending=true;s_ble.host_started=false;
    ble_release_advertiser(&s_ble);
    assert(!terminated && !s_ble.connections[0].allocated);
}
''')

    def converter(self):
        # Object allocation and property APIs are fault boundaries; ownership
        # decisions execute the actual production converter and drop callback.
        return r'''
typedef void JSContext;
typedef int JSValue;
typedef struct { JSValue val; } JSGCRef;
#define JS_UNDEFINED 0
#define JS_EXCEPTION -1
#define JS_IsException(v) ((v)==JS_EXCEPTION)
#define JS_IsUndefined(v) ((v)==JS_UNDEFINED)
#define JS_CLASS_BLE_CONNECTION 2
static int s_ble_closed_connection_ref;
static int calls,fail_at,roots,retired;
static JSGCRef *stack[2];
static JSValue *JS_PushGCRef(void *ctx,JSGCRef *r) { (void)ctx;assert(roots<2);stack[roots++]=r;return &r->val; }
static JSValue JS_PopGCRef(void *ctx,JSGCRef *r) { (void)ctx;assert(roots && stack[--roots]==r);return r->val; }
static int allocate(void) { return ++calls==fail_at ? JS_EXCEPTION : 42; }
static JSValue ble_new_connection_handle(void *ctx,unsigned i,ble_connection_slot_t *s) { (void)ctx;(void)i;(void)s;return allocate(); }
static JSValue JS_NewObject(void *ctx) { (void)ctx;return allocate(); }
static JSValue JS_NewString(void *ctx,const char *s) { (void)ctx;(void)s;return allocate(); }
static JSValue JS_NewUint32(void *ctx,unsigned n) { (void)ctx;(void)n;return allocate(); }
static JSValue JS_NewInt64(void *ctx,int64_t n) { (void)ctx;(void)n;return allocate(); }
static bool esp32_mquickjs_set_property_ref(void *ctx,JSValue *v,const char *key,JSValue value) {
    (void)ctx;(void)v;(void)key;return value!=JS_EXCEPTION && allocate()!=JS_EXCEPTION;
}
static void JS_ThrowReferenceError(void *ctx,const char *s) { (void)ctx;(void)s; }
static void ble_retire_handle(void *ctx,JSValue value,int cls,void *ref) {
    (void)ctx;(void)value;(void)cls;(void)ref;retired++;
}
'''+function(BLE.read_text(),'ble_advertiser_event_to_js')

    def test_conversion_failures_drop_only_unclaimed_owner_and_retire_partial_handle(self):
        fixture=FIXTURE.replace('static void ble_advertiser_event_to_js(void) {}','')
        # Factory is not used here; its converter prototype is an SDK boundary.
        compile_run(self,fixture+self.production()+self.converter()+r'''
int main(void) {
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        s_ble.advertiser.generation=4;
        s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
            .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
        s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,.conn_handle=12};
        ble_advertiser_event_t event={.generation=4,.connection_generation=7};
        calls=retired=terminated=0;fail_at=nth;
        JSValue result=ble_advertiser_event_to_js(NULL,&event,&s_ble.advertiser);
        assert(!roots && !s_ble.connections[0].incoming_pending);
        if(!nth) {
            total=calls;assert(result!=JS_EXCEPTION && !retired && !terminated);
            ble_release_advertiser(&s_ble);assert(!terminated);
        } else {
            assert(result==JS_EXCEPTION && terminated==1 && last_handle==11);
            assert(retired==(nth==1 ? 0 : 1));
            assert(s_ble.connections[0].release_on_disconnect);
            ble_advertiser_event_drop(&event,&s_ble.advertiser);assert(terminated==1);
        }
        assert(s_ble.connections[1].open && !s_ble.connections[1].release_on_disconnect);
    }
}
''')

    def test_late_receive_finish_after_close_cannot_transfer_or_discard_new_owner(self):
        fixture=FIXTURE.replace('static void ble_advertiser_event_to_js(void) {}','')
        compile_run(self,fixture+self.production()+self.converter()+r'''
int main(void) {
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    ble_advertiser_event_t old={.generation=4,.connection_generation=7};
    ble_release_advertiser(&s_ble);
    assert(ble_advertiser_event_to_js(NULL,&old,&s_ble.advertiser)==JS_EXCEPTION);
    assert(!roots && !calls && terminated==1);
    s_ble.advertiser.generation=5;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=8,.incoming_advertiser_generation=5,.conn_handle=12};
    assert(ble_advertiser_event_to_js(NULL,&old,&s_ble.advertiser)==JS_EXCEPTION);
    assert(!roots && !calls && terminated==1 && s_ble.connections[0].incoming_pending);
}
''')

    def test_saturated_queue_terminates_only_its_unclaimed_incoming_connection(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    s_ble.advertiser.generation=4;s_ble.advertiser.active=true;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=7,.conn_handle=11};
    s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,.conn_handle=12};
    queue_full=true;terminate_result=17;
    ble_publish_incoming_connection(&s_ble.connections[0],0);
    assert(!s_ble.advertiser.active && s_ble.advertiser.stop_reason==BLE_STOP_CONNECTED);
    assert(s_ble.advertiser.incoming==1 && s_ble.advertiser.dropped==1);
    assert(terminated==1 && last_handle==11 && s_ble.rejected_connection_error==17);
    assert(s_ble.connections[0].allocated && s_ble.connections[0].release_on_disconnect);
    assert(!s_ble.connections[0].incoming_pending && s_ble.connections[1].open);
    /* A successfully enqueued connection remains queue-owned until conversion. */
    queue_full=false;ble_publish_incoming_connection(&s_ble.connections[1],1);
    assert(s_ble.connections[1].incoming_pending && terminated==1);
    assert(queued.connection_index==1 && queued.connection_generation==0 && queued.generation==4);
    assert(s_ble.advertiser.incoming==2 && s_ble.advertiser.dropped==1);
}
''')

    def queue_future(self):
        source=(BLE.parents[2]/'core/esp32_mquickjs_event_queue.c').read_text()
        return r'''
#define JS_NULL 1
typedef struct {
    JSValue (*to_js)(JSContext *,const void *,void *);
    void (*drop)(void *,void *);
    void *opaque;
} esp32_mquickjs_event_queue_t;
typedef struct {
    esp32_mquickjs_event_queue_t *queue;
    bool native_queue_retained,received,event_finished,queue_retained;
    void *event,*timer,*ctx;
    JSGCRef queue_ref;
} esp32_mquickjs_future_driver_state_t;
static int released_queue,released_storage;
static void esp_timer_stop(void *p) { (void)p; }
static void esp_timer_delete(void *p) { (void)p; }
static void JS_DeleteGCRef(void *ctx,JSGCRef *ref) { (void)ctx;(void)ref; }
static void heap_caps_free(void *p) { (void)p;released_storage++; }
static void event_queue_resource_release(void *p,void *queue) { (void)queue;heap_caps_free(p); }
static void esp32_mquickjs_event_queue_release(void *q) { (void)q;released_queue++; }
'''+function(source,'event_queue_future_finish')+function(source,'event_queue_future_destroy')

    def test_production_receive_destroy_drops_dequeued_native_owner_once(self):
        fixture=FIXTURE.replace('static void ble_advertiser_event_to_js(void) {}','')
        compile_run(self,fixture+self.production()+self.converter()+self.queue_future()+r'''
int main(void) {
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    ble_advertiser_event_t event={.generation=4,.connection_generation=7};
    esp32_mquickjs_event_queue_t queue={ble_advertiser_event_to_js,ble_advertiser_event_drop,&s_ble.advertiser};
    esp32_mquickjs_future_driver_state_t state={.queue=&queue,.received=true,
        .native_queue_retained=true,.event=&event};
    event_queue_future_destroy(&state);
    assert(terminated==1 && released_storage==2 && released_queue==1);
    assert(s_ble.connections[0].allocated && s_ble.connections[0].release_on_disconnect);
    ble_release_advertiser(&s_ble);assert(terminated==1);
}
''')

    def test_production_receive_finish_failure_owns_cleanup_before_destroy(self):
        fixture=FIXTURE.replace('static void ble_advertiser_event_to_js(void) {}','')
        compile_run(self,fixture+self.production()+self.converter()+self.queue_future()+r'''
int main(void) {
    s_ble.advertiser.generation=4;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
        .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
    ble_advertiser_event_t event={.generation=4,.connection_generation=7};
    esp32_mquickjs_event_queue_t queue={ble_advertiser_event_to_js,ble_advertiser_event_drop,&s_ble.advertiser};
    esp32_mquickjs_future_driver_state_t state={.queue=&queue,.received=true,
        .native_queue_retained=true,.event=&event};
    fail_at=3;
    assert(event_queue_future_finish(NULL,&state)==JS_EXCEPTION);
    assert(state.event_finished && terminated==1 && retired==1 && !roots);
    event_queue_future_destroy(&state);
    assert(terminated==1 && released_storage==2 && released_queue==1);
}
''')
