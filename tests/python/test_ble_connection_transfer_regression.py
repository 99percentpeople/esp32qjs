"""Execute production BLE connection ownership and queue handoff paths."""
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
#define BLE_HS_EINVAL 1
#define portMAX_DELAY 0
static int s_ble_connection_mutex,connection_lock_depth;
#define xSemaphoreTakeRecursive(m,t) ((void)(m),(void)(t),connection_lock_depth++)
#define xSemaphoreGiveRecursive(m) ((void)(m),assert(connection_lock_depth>0),connection_lock_depth--)
#define BLE_HS_ENOTCONN 2
#define BLE_ERR_REM_USER_CONN_TERM 3
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
#define JS_EXCEPTION -1
#define JS_IsException(v) ((v)==JS_EXCEPTION)
typedef void JSContext;
typedef int JSValue;
typedef struct { unsigned connection_index,connection_generation; int host_code; bool transferred; } esp32_mquickjs_future_driver_state_t;
typedef struct { bool allocated,open,reserved,release_on_disconnect; unsigned generation; int conn_handle,active_gap_state,connect_operation; } ble_connection_slot_t;
static struct { unsigned max_connections; ble_connection_slot_t connections[2]; atomic_int rejected_connection_error; } s_ble={.max_connections=2};
static int result=JS_EXCEPTION,terminate_result,terminated,released;
static JSValue ble_throw_error(void *ctx,const char *code,int rc,int a,int b,int c) { (void)ctx;(void)code;(void)rc;(void)a;(void)b;(void)c;return JS_EXCEPTION; }
static JSValue ble_new_connection_handle(void *ctx,unsigned i,ble_connection_slot_t *s) { (void)ctx;(void)i;(void)s;return result; }
static void ble_gap_event_callback(void) {}
static int ble_gap_set_event_cb(int handle,void (*cb)(void),void *arg) { (void)handle;(void)cb;(void)arg;return 0; }
static void ble_active_state_bind(int *slot,void *state) { (void)slot;(void)state; }
static int ble_gap_terminate(int handle,int reason) { (void)handle;(void)reason;terminated++;return terminate_result; }
static bool esp32_mquickjs_wireless_native_operation_begin(int *state) { *state=1;return true; }
static bool esp32_mquickjs_wireless_native_operation_request_cancel(int *state) { (void)state;return true; }
static bool esp32_mquickjs_wireless_native_operation_complete(int *state) { *state=0;return true; }
static bool esp32_mquickjs_wireless_native_operation_is_quiescent(int *state) { return *state==0; }
static void ble_future_state_release(void *state) { (void)state;released++; }
'''

class BleConnectionTransferRegression(unittest.TestCase):
    def production(self):
        source=BLE.read_text()
        return function(source,'ble_terminate_unclaimed_connection')+function(source,'ble_connect_finish')+function(source,'ble_connect_destroy')

    def test_js_allocation_failure_does_not_transfer_native_connection(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    assert(ble_connect_finish(NULL,&state)==JS_EXCEPTION);
    assert(!state.transferred);
    ble_connect_destroy(&state);
    assert(terminated==1 && s_ble.connections[0].allocated);
    assert(s_ble.connections[0].release_on_disconnect);
}
''')

    def test_failed_terminate_preserves_slot_until_native_terminal(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    terminate_result=17;
    ble_connect_destroy(&state);
    assert(terminated==1 && released==1);
    assert(s_ble.connections[0].open && s_ble.connections[0].allocated);
    assert(s_ble.connections[0].release_on_disconnect);
    assert(!esp32_mquickjs_wireless_native_operation_is_quiescent(&s_ble.connections[0].connect_operation));
}
''')

    def test_successful_handoff_and_stale_state_do_not_disconnect_live_owner(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    result=42;
    assert(ble_connect_finish(NULL,&state)==42 && state.transferred);
    ble_connect_destroy(&state);
    assert(terminated==0);
    state.transferred=false;state.connection_generation=0;
    ble_connect_destroy(&state);
    assert(terminated==0 && s_ble.connections[0].open);
}
''')

    def test_cleanup_retry_releases_only_after_native_absence_is_confirmed(self):
        compile_run(self,FIXTURE+self.production()+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    ble_connection_slot_t *slot=&s_ble.connections[0];
    *slot=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    terminate_result=17;ble_connect_destroy(&state);
    assert(slot->allocated && slot->connect_operation && s_ble.rejected_connection_error==17);
    terminate_result=0;ble_terminate_unclaimed_connection(slot, slot->generation);
    assert(slot->allocated && slot->open && slot->connect_operation);
    terminate_result=BLE_HS_ENOTCONN;ble_terminate_unclaimed_connection(slot, slot->generation);
    assert(!slot->allocated && !slot->open && !slot->connect_operation);
    assert(!slot->release_on_disconnect && slot->conn_handle==BLE_INVALID_CONN_HANDLE);
    assert(terminated==3 && s_ble.rejected_connection_error==17);
}
''')

    def test_runtime_teardown_retries_unclaimed_termination_before_callback_barrier(self):
        source=BLE.read_text().replace('bool esp32_mquickjs_deinit_ble_runtime(',
                                      'static bool esp32_mquickjs_deinit_ble_runtime(')
        fixture=FIXTURE.replace('unsigned max_connections;',
            'int lifecycle;void *runtime;struct { bool active; } scanner,advertiser;unsigned max_connections;')
        compile_run(self,fixture+self.production()+r'''
#define BLE_LIFECYCLE_CLOSED 0
#define BLE_LIFECYCLE_FAILED 2
#define BLE_CONNECT_CANCEL_QUIESCE_MS 10
#define ESP_LOGE(...) ((void)0)
static struct { atomic_bool worker_submitted,worker_completed; } s_ble_orphan_close;
static int cleaned,freed,reset,cleanup_result;
static void vTaskDelay(int n) { (void)n;assert(0); }
static int ble_gap_disc_cancel(void) { return 0; }
static int ble_gap_adv_stop(void) { return 0; }
static bool ble_wait_for_pending_connects(int ms) {
    (void)ms;assert(terminated>0);
    return s_ble.connections[0].connect_operation==0;
}
static int ble_cleanup_native(void *ad) { (void)ad;cleaned++;return cleanup_result; }
static void ble_free_pools(void *ad) { (void)ad;freed++; }
static void ble_reset_orphan_close(void) { reset++; }
'''+function(source,'esp32_mquickjs_deinit_ble_runtime')+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.lifecycle=1;s_ble.runtime=(void *)1;
    s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.generation=1,.conn_handle=7};
    s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,.conn_handle=8};
    terminate_result=17;ble_connect_destroy(&state);
    assert(!esp32_mquickjs_deinit_ble_runtime(NULL));
    assert(terminated==2 && !cleaned && !freed && !reset && s_ble.runtime);
    assert(s_ble.lifecycle==BLE_LIFECYCLE_FAILED);
    terminate_result=BLE_HS_ENOTCONN;cleanup_result=18;
    assert(!esp32_mquickjs_deinit_ble_runtime(NULL));
    assert(terminated==3 && cleaned==1 && !freed && !reset && s_ble.runtime);
    assert(s_ble.connections[1].open && !s_ble.connections[1].release_on_disconnect);
    cleanup_result=0;
    assert(esp32_mquickjs_deinit_ble_runtime(NULL));
    assert(terminated==3 && cleaned==2 && freed==1 && reset==1 && !s_ble.runtime);
    assert(s_ble.lifecycle==BLE_LIFECYCLE_CLOSED);
}
''')

    def test_closed_result_does_not_rebind_a_reused_native_handle(self):
        fixture=FIXTURE.replace('bool transferred;', 'bool transferred;struct { int val; } owner_ref;').replace('static int result=JS_EXCEPTION,', 'static int callback_rebinds;\nstatic int result=JS_EXCEPTION,').replace('(void)handle;(void)cb;(void)arg;return 0;', '(void)handle;(void)cb;(void)arg;callback_rebinds++;return 0;')
        compile_run(self,fixture+r'''
#define JS_TRUE 1
#define JS_CLASS_BLE_CONNECTION 2
static int s_ble_closed_connection_ref;
static JSValue ble_bool_finish(void *ctx,void *s) { (void)ctx;(void)s;return JS_TRUE; }
static void ble_native_state_complete(void *s) { (void)s; }
static void ble_recycle_connection(ble_connection_slot_t *slot) { slot->allocated=false; }
static void ble_retire_handle(void *ctx,int value,int cls,void *ref) { (void)ctx;(void)value;(void)cls;(void)ref; }
'''+function(BLE.read_text(),'ble_connection_close_finish')+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.connection_generation=1};
    s_ble.connections[0]=(ble_connection_slot_t){.open=false,.allocated=true,.generation=1,.conn_handle=7};
    assert(ble_connection_close_finish(NULL,&state)==JS_TRUE);
    assert(!callback_rebinds && !s_ble.connections[0].allocated);
}
''')
