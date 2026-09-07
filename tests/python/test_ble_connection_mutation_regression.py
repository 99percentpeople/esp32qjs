"""Execute production connection mutation against controlled Host schedules."""
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from test_wireless_control_regression import function,compile_run
BLE=pathlib.Path(__file__).resolve().parents[2]/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
SDK=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#define BLE_HS_ENOTCONN 2
#define BLE_ERR_REM_USER_CONN_TERM 3
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
#define portMAX_DELAY 0
static pthread_mutex_t mutex;
static pthread_mutex_t *s_ble_connection_mutex=&mutex;
static int xSemaphoreTakeRecursive(pthread_mutex_t *m,int wait) { (void)wait;return pthread_mutex_lock(m)==0; }
static int xSemaphoreGiveRecursive(pthread_mutex_t *m) { return pthread_mutex_unlock(m)==0; }
typedef struct { bool open,allocated,reserved,release_on_disconnect; unsigned generation; int conn_handle,connect_operation; } ble_connection_slot_t;
static struct { atomic_int rejected_connection_error; } s_ble;
static ble_connection_slot_t slot={.open=true,.allocated=true,.generation=1,.conn_handle=7};
static atomic_bool callback_waiting,callback_attempted,callback_done;
static bool contended,blocked_attempt;
static int calls,driver_result=BLE_HS_ENOTCONN;
static bool esp32_mquickjs_wireless_native_operation_complete(int *s) { *s=0;return true; }
static int ble_gap_terminate(int h,int reason) {
    (void)reason;assert(h==7);calls++;
    if(contended) {
        callback_waiting=true;
        /* Callback attempts the same framework lock while native I/O runs. */
        while(!callback_attempted) sched_yield();
    }
    return driver_result;
}
static void *host(void *arg) {
    (void)arg;
    while(!callback_waiting) usleep(1000);
    int rc=pthread_mutex_trylock(s_ble_connection_mutex);
    assert(rc==0 || rc==EBUSY);
    if(rc==EBUSY) {
        blocked_attempt=true;
        callback_attempted=true;
        xSemaphoreTakeRecursive(s_ble_connection_mutex,0);
    }
    /* SDK boundary: disconnect then another native connection with reused handle. */
    slot=(ble_connection_slot_t){.open=true,.allocated=true,.generation=2,.conn_handle=7};
    callback_done=true;
    callback_attempted=true;
    xSemaphoreGiveRecursive(s_ble_connection_mutex);
    return NULL;
}
static void init(void) {
    pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&mutex,&attr));pthread_mutexattr_destroy(&attr);
}
'''
class BleConnectionMutationRegression(unittest.TestCase):
    def production(self):
        return function(BLE.read_text(),'ble_terminate_unclaimed_connection')

    def test_disconnect_and_reuse_cannot_interleave_with_native_termination(self):
        compile_run(self,SDK+self.production()+r'''
int main(void) {
    init();contended=true;
    pthread_t thread;assert(!pthread_create(&thread,NULL,host,NULL));
    ble_terminate_unclaimed_connection(&slot, 1);
    assert(!pthread_join(thread,NULL));
    assert(slot.generation==2 && slot.open && slot.allocated && !slot.release_on_disconnect);
    assert(calls==1);
}
''')

    def test_stale_generation_and_already_closed_connection_skip_native_mutation(self):
        compile_run(self,SDK+self.production()+r'''
int main(void) {
    init();slot.generation=2;
    ble_terminate_unclaimed_connection(&slot,1);
    assert(!calls && slot.open && !slot.release_on_disconnect);
    slot.open=false;ble_terminate_unclaimed_connection(&slot,2);
    assert(!calls && !slot.release_on_disconnect);
}
''')

    def test_production_gap_entry_uses_same_lock_and_supports_callback_reentry(self):
        dispatch=r'''
struct ble_gap_event { int type; };
static int ble_gap_event_callback_locked(struct ble_gap_event *e,void *arg) {
    (void)e;(void)arg;
    /* A callback may synchronously reject a connection using this same lock. */
    driver_result=0;ble_terminate_unclaimed_connection(&slot,1);
    return 42;
}
'''
        compile_run(self,SDK+self.production()+dispatch+function(BLE.read_text(),'ble_gap_event_callback')+r'''
int main(void) {
    init();struct ble_gap_event e={0};
    assert(ble_gap_event_callback(&e,NULL)==42);
    assert(calls==1 && slot.release_on_disconnect);
    assert(!pthread_mutex_trylock(&mutex));pthread_mutex_unlock(&mutex);
}
''')

    def test_registered_connection_start_paths_hold_guard_through_submission(self):
        for name in ['ble_connect_start','ble_pair_start','ble_exchange_mtu_start',
                     'ble_read_rssi_start','ble_discover_start','ble_gatt_read_start',
                     'ble_gatt_write_start','ble_subscribe_start','ble_server_notify_start']:
            with self.subTest(start=name):
                declarations=r'''
typedef void JSContext;
typedef void esp32_mquickjs_runtime_t;
typedef void esp32_mquickjs_future_driver_state_t;
typedef int esp32_mquickjs_future_token_t;
static bool START_locked(JSContext *ctx,esp32_mquickjs_runtime_t *r,
    esp32_mquickjs_future_token_t t,esp32_mquickjs_future_driver_state_t *s) {
    (void)ctx;(void)r;(void)t;(void)s;
    return ble_gap_terminate(7,3)==0;
}
'''.replace('START',name)
                main=r'''
int main(void) {
    init();contended=true;driver_result=0;
    pthread_t thread;assert(!pthread_create(&thread,NULL,host,NULL));
    assert(START(NULL,NULL,1,NULL));
    assert(!pthread_join(thread,NULL));
    assert(blocked_attempt && calls==1);
    contended=false;driver_result=2;
    assert(!START(NULL,NULL,1,NULL));
    assert(!pthread_mutex_destroy(&mutex));
}
'''.replace('START',name)
                compile_run(self,SDK+declarations+function(BLE.read_text(),name)+main)
