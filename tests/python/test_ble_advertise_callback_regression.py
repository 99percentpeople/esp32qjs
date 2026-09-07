"""Exercise production advertiser start/release across SDK callback schedules."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from test_wireless_control_regression import function, compile_run

BLE = pathlib.Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#ifdef THREADED
#include <pthread.h>
#include <sched.h>
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t schedule_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t schedule_cond=PTHREAD_COND_INITIALIZER;
static bool entered,resume_callback;
#endif
#define portMAX_DELAY 0
static int s_ble_connection_mutex;
#define xSemaphoreTakeRecursive(m,t) ((void)(m),(void)(t))
#define xSemaphoreGiveRecursive(m) ((void)(m))
#define BLE_LIFECYCLE_ACTIVE 1
#define BLE_HS_EINVAL 9
#define BLE_STOP_ERROR 1
#define BLE_HS_EBUSY 2
#define BLE_HS_ENOTCONN 3
#define BLE_ERR_REM_USER_CONN_TERM 4
#define BLE_GAP_EVENT_ADV_COMPLETE 5
#define BLE_GAP_EVENT_CONNECT 6
#define BLE_GAP_EVENT_DISCONNECT 7
#define BLE_GAP_EVENT_MTU 8
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
typedef void JSContext;
typedef void esp32_mquickjs_runtime_t;
typedef int esp32_mquickjs_future_token_t;
typedef struct { bool allocated,active,queue_rooted; int generation,stop_reason,queue_ref; void *queue; } ble_advertiser_t;
typedef struct { bool allocated,reserved,open,release_on_disconnect; uint16_t conn_handle; } ble_connection_slot_t;
typedef struct { void *ctx; int lock,own_addr_type,lifecycle,generation; ble_advertiser_t advertiser; atomic_int rejected_connection_error; } ble_adapter_t;
typedef struct { void *runtime; int token,adapter_generation,connection_generation; bool started; uint8_t adv_data[31],scan_response[31]; uint8_t adv_data_length,scan_response_length; int duration_ms,adv_params,host_code; atomic_bool completed; } esp32_mquickjs_future_driver_state_t;
static ble_adapter_t s_ble={.lifecycle=BLE_LIFECYCLE_ACTIVE};
struct ble_gap_event { int type; struct { int status; uint16_t conn_handle; } connect; };
typedef int (*callback_t)(struct ble_gap_event *,void *);
static callback_t saved_callback;
static void *saved_arg;
static int immediate,driver_result,data_result,rsp_result,driver_calls,delivered,closed,wakes,terminated,terminate_result;
static uint16_t terminated_handle;
static uintptr_t s_ble_advertise_next_identity=1,s_ble_advertise_callback_identity;
static uint32_t s_ble_advertise_callback_refs;
static _Thread_local int lock_depth;
#ifdef THREADED
#define taskENTER_CRITICAL(lock) do { (void)(lock);pthread_mutex_lock(&state_lock);assert(lock_depth++==0); } while(0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock);assert(--lock_depth==0);pthread_mutex_unlock(&state_lock); } while(0)
#else
#define taskENTER_CRITICAL(lock) do { (void)(lock);assert(lock_depth++==0); } while(0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock);assert(--lock_depth==0); } while(0)
#endif
static void vTaskDelay(int ticks) {
    (void)ticks;assert(!lock_depth);
#ifdef THREADED
    assert(closed==0);
    pthread_mutex_lock(&schedule_lock);
    resume_callback=true;
    pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    sched_yield();
#endif
}
static ble_connection_slot_t existing,rejected;
static bool no_slot;
static ble_connection_slot_t *ble_find_connection(uint16_t handle,uint16_t *index) {
    (void)index;
    if(existing.allocated && existing.conn_handle==handle) return &existing;
    if(rejected.allocated && rejected.conn_handle==handle) return &rejected;
    return NULL;
}
static ble_connection_slot_t *ble_reserve_connection(bool central,uint16_t *index) {
    (void)central;(void)index;
    return no_slot ? NULL : &rejected;
}
static int ble_gap_terminate(uint16_t handle,int reason) {
    (void)reason;assert(!lock_depth);terminated++;terminated_handle=handle;return terminate_result;
}
static int ble_gap_event_callback(struct ble_gap_event *event,void *arg) {
    (void)arg;
#ifdef THREADED
    pthread_mutex_lock(&schedule_lock);
    entered=true;
    pthread_cond_broadcast(&schedule_cond);
    while(!resume_callback) pthread_cond_wait(&schedule_cond,&schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    assert(closed==0);
#endif
    if(event->type==BLE_GAP_EVENT_ADV_COMPLETE) s_ble.advertiser.active=false;
    else delivered++;
    return 0;
}
static int ble_gap_adv_set_data(const uint8_t *p,int n) { (void)p;(void)n;return data_result; }
static int ble_gap_adv_rsp_set_data(const uint8_t *p,int n) { (void)p;(void)n;return rsp_result; }
static int ble_gap_adv_start(int addr,void *peer,int duration,void *params,callback_t cb,void *arg) {
    (void)addr;(void)peer;(void)duration;(void)params;driver_calls++;
    saved_callback=cb;saved_arg=arg;
    if(immediate) { struct ble_gap_event event={.type=BLE_GAP_EVENT_ADV_COMPLETE};cb(&event,arg); }
    return driver_result;
}
static void ble_throw_error(void *ctx,const char *code,int rc,int a,int b,int c) { (void)ctx;(void)code;(void)rc;(void)a;(void)b;(void)c; }
static bool esp32_mquickjs_future_wake(void *r,int t) { (void)r;(void)t;wakes++;return true; }
static void ble_discard_advertiser_connections(void *ad) { (void)ad; }
static void ble_release_event_queue(void *ctx,void **q,int *ref,bool *rooted) { (void)ctx;(void)q;(void)ref;(void)rooted;assert(!s_ble_advertise_callback_refs);closed++; }
'''
MAIN = r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={0};
    immediate=IMMEDIATE;
    assert(ble_advertise_start(NULL,NULL,1,&state));
    if(immediate) { assert(!s_ble.advertiser.active);return 0; }
    callback_t old_cb=saved_callback; void *old_arg=saved_arg;
    /* Native stop succeeded after it copied the old cb/arg. */
    s_ble.advertiser.active=false;
    ble_release_advertiser(&s_ble);
    assert(ble_advertise_start(NULL,NULL,2,&state));
    struct ble_gap_event event={.type=BLE_GAP_EVENT_ADV_COMPLETE};
    old_cb(&event,old_arg);
    assert(s_ble.advertiser.active);
}
'''

class BleAdvertiseCallbackRegression(unittest.TestCase):
    def test_queued_start_revalidates_adapter_and_advertiser_before_driver_mutation(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={0};
    s_ble.lifecycle=0;
    assert(!ble_advertise_start(NULL,NULL,1,&state));
    s_ble.lifecycle=BLE_LIFECYCLE_ACTIVE;
    s_ble.generation=1;
    assert(!ble_advertise_start(NULL,NULL,1,&state));
    state.adapter_generation=1;s_ble.advertiser.generation=1;
    assert(!ble_advertise_start(NULL,NULL,1,&state));
    assert(driver_calls==0 && s_ble_advertise_next_identity==1);
}
''')

    def test_close_drains_entered_advertiser_callback_before_queue_release(self):
        import shutil
        import subprocess
        import tempfile
        main=r'''
static void *callback_thread(void *arg) {
    (void)arg;
    struct ble_gap_event event={.type=BLE_GAP_EVENT_ADV_COMPLETE};
    saved_callback(&event,saved_arg);return NULL;
}
int main(void) {
    esp32_mquickjs_future_driver_state_t state={0};
    assert(ble_advertise_start(NULL,NULL,1,&state));
    pthread_t thread;
    assert(!pthread_create(&thread,NULL,callback_thread,NULL));
    pthread_mutex_lock(&schedule_lock);
    while(!entered) pthread_cond_wait(&schedule_cond,&schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    assert(ble_advertise_callback_bind()==NULL);
    ble_release_advertiser(&s_ble);
    assert(!pthread_join(thread,NULL));
    assert(closed==1 && !s_ble_advertise_callback_refs && !s_ble_advertise_callback_identity);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            source=pathlib.Path(temporary)/'race.c'
            source.write_text('#define THREADED\n'+FIXTURE+self.production()+main)
            result=subprocess.run([shutil.which('cc') or 'cc','-std=c11','-pthread',
                str(source),'-o',temporary+'/test'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([temporary+'/test'],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)

    def production(self):
        source=BLE.read_text()
        return '\n'.join(function(source,name) for name in (
            'ble_reject_incoming_connection','ble_advertise_callback_bind',
            'ble_advertise_event_callback','ble_advertise_callbacks_quiesce',
            'ble_release_advertiser','ble_advertise_start'))

    def test_late_connect_rejects_only_unclaimed_connection_and_preserves_later_events(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={0};
    assert(ble_advertise_start(NULL,NULL,1,&state));
    callback_t old_cb=saved_callback;void *old_arg=saved_arg;
    ble_release_advertiser(&s_ble);
    assert(ble_advertise_start(NULL,NULL,2,&state));
    existing=(ble_connection_slot_t){.allocated=true,.open=true,.conn_handle=9};
    struct ble_gap_event event={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=7}};
    old_cb(&event,old_arg);
    assert(terminated==1 && terminated_handle==7 && delivered==0);
    assert(rejected.allocated && rejected.open && rejected.release_on_disconnect);
    assert(existing.open && s_ble.advertiser.active);
    event.connect.conn_handle=9;
    old_cb(&event,old_arg);
    assert(terminated==1); /* Duplicate for an already handed-off live owner. */
    event.type=BLE_GAP_EVENT_DISCONNECT;
    old_cb(&event,old_arg);
    event.type=BLE_GAP_EVENT_MTU;
    old_cb(&event,old_arg);
    assert(delivered==2); /* Connections keep their inherited callback. */
}
''')

    def test_rejection_failure_retains_native_owner_and_records_error(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    terminate_result=17;
    ble_reject_incoming_connection(7);
    assert(rejected.allocated && rejected.open && rejected.release_on_disconnect);
    assert(s_ble.rejected_connection_error==17);
    /* A closed JS handle must not shield a reused native handle from rejection. */
    existing=(ble_connection_slot_t){.allocated=true,.open=false,.conn_handle=8};
    no_slot=true;
    ble_reject_incoming_connection(8);
    assert(terminated_handle==8 && terminated==2);
    assert(s_ble.rejected_connection_error==17);
    no_slot=false;terminate_result=BLE_HS_ENOTCONN;
    memset(&rejected,0,sizeof(rejected));
    ble_reject_incoming_connection(10);
    assert(!rejected.allocated && !rejected.open);
    assert(rejected.conn_handle==BLE_INVALID_CONN_HANDLE);
}
''')

    def test_submission_failures_retire_cookie_and_exhaustion_skips_native_start(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.scan_response_length=1};
    for(int stage=0;stage<3;stage++) {
        data_result=stage==0 ? 17 : 0;
        rsp_result=stage==1 ? 17 : 0;
        driver_result=stage==2 ? 17 : 0;
        assert(!ble_advertise_start(NULL,NULL,1,&state));
        assert(!s_ble.advertiser.active && s_ble_advertise_callback_identity==0);
    }
    driver_result=0;s_ble_advertise_next_identity=UINTPTR_MAX;
    assert(ble_advertise_start(NULL,NULL,1,&state));
    assert((uintptr_t)saved_arg==UINTPTR_MAX);
    ble_release_advertiser(&s_ble);
    int previous=driver_calls;
    assert(!ble_advertise_start(NULL,NULL,1,&state));
    assert(driver_calls==previous && s_ble_advertise_next_identity==0);
}
''')

    def test_synchronous_completion_is_not_overwritten(self):
        compile_run(self,FIXTURE+self.production()+MAIN.replace('IMMEDIATE','1'))

    def test_old_completion_does_not_stop_reopened_advertiser(self):
        compile_run(self,FIXTURE+self.production()+MAIN.replace('IMMEDIATE','0'))
