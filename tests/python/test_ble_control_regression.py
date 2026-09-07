"""Run production BLE callback bodies with a deterministic native fixture.

SDK submission notifications and ATT confirmations are separate events. The
fixture replaces SDK/RTOS boundaries, not the production completion branch.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BLE = ROOT / 'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'


class BleControlRegression(unittest.TestCase):
    def test_indication_submission_is_not_confirmation(self):
        source = BLE.read_text()
        start = source.index('    case BLE_GAP_EVENT_NOTIFY_TX: {')
        end = source.index('    case BLE_GAP_EVENT_SUBSCRIBE:', start)
        branch = source[start:end]
        fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#define BLE_GAP_EVENT_NOTIFY_TX 1
#define BLE_HS_EDONE 14
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS 2
#define taskENTER_CRITICAL(x) ((void)0)
#define taskEXIT_CRITICAL(x) ((void)0)
typedef struct esp32_mquickjs_future_driver_state {
    bool indication, notify_submitting, detached;
    uint16_t attribute_handle;
    uint16_t notify_handles[2];
    bool notify_pending[2];
    _Atomic uint16_t pending_confirmations;
    int host_code, token;
    void *runtime;
    _Atomic bool completed;
} esp32_mquickjs_future_driver_state_t;
static struct { int lock; } s_ble;
static _Atomic(esp32_mquickjs_future_driver_state_t *) s_ble_server_notify_state;
static int wakes;
static esp32_mquickjs_future_driver_state_t *ble_active_state_acquire(
    _Atomic(esp32_mquickjs_future_driver_state_t *) *p) { return atomic_load(p); }
static void ble_future_state_drop(esp32_mquickjs_future_driver_state_t *s) { (void)s; }
static void ble_native_state_complete(esp32_mquickjs_future_driver_state_t *s) { (void)s; atomic_store(&s_ble_server_notify_state, NULL); }
static int esp32_mquickjs_future_wake(void *r, int t) { (void)r; (void)t; wakes++; return 1; }
static void ble_future_wake_state(esp32_mquickjs_future_driver_state_t *s) { if (!s->detached) esp32_mquickjs_future_wake(s->runtime, s->token); }
struct event { int type; struct { bool indication; uint16_t attr_handle, conn_handle; int status; } notify_tx; };
static void callback(struct event *event) { switch (event->type) {
'''
        main = r'''
} }
int main(void) {
    esp32_mquickjs_future_driver_state_t a = {.indication=true, .attribute_handle=42,
        .pending_confirmations=1, .notify_handles={7,0}, .notify_pending={true,false}};
    atomic_store(&s_ble_server_notify_state, &a);
    struct event event = {.type=BLE_GAP_EVENT_NOTIFY_TX, .notify_tx={true,42,7,0}};
    callback(&event);
    assert(!atomic_load(&a.completed));
    assert(atomic_load(&a.pending_confirmations)==1);
    assert(wakes==0);
    event.notify_tx.status=BLE_HS_EDONE;
    event.notify_tx.conn_handle=8;
    callback(&event);
    assert(!atomic_load(&a.completed));
    event.notify_tx.conn_handle=7;
    callback(&event);
    assert(atomic_load(&a.completed));
    assert(atomic_load(&a.pending_confirmations)==0);
    callback(&event);
    assert(atomic_load(&a.pending_confirmations)==0);
    assert(wakes==1);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c = pathlib.Path(tmp) / 'callback.c'
            c.write_text(fixture + branch + main)
            result = subprocess.run([shutil.which('cc') or 'cc', '-std=c11', str(c), '-o', tmp+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([tmp+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_native_cookie_detach_late_callback_and_host_barrier(self):
        import re
        source = BLE.read_text()
        names = ['ble_active_state_bind', 'ble_gatt_state_bind',
                 'ble_active_state_acquire', 'ble_future_state_hold',
                 'ble_future_state_drop', 'ble_operation_acquire',
                 'ble_native_state_complete', 'ble_native_states_quiesced',
                 'ble_gatt_callback_drop', 'ble_future_wake_state',
                 'ble_future_state_release', 'ble_exchange_mtu_callback',
                 'ble_discover_start_descriptors', 'ble_discover_complete',
                 'ble_discover_descriptor_callback', 'ble_discover_characteristic_callback',
                 'ble_discover_service_callback']
        bodies = []
        for name in names:
            match = re.search(r'static [^;{}]*\b' + name + r'\([^;{}]*\)\n\{', source)
            self.assertIsNotNone(match, name)
            end = source.index('\n}\n', match.start()) + 3
            bodies.append(source[match.start():end])
        with tempfile.TemporaryDirectory() as tmp:
            c = pathlib.Path(tmp) / 'lifecycle.c'
            c.write_text('#include "ble_lifecycle_test_support.h"\n' + '\n'.join(bodies) +
                         '\n#include "ble_lifecycle_cases.inc"\n')
            core = ROOT/'components/esp32_mquickjs/src/core'
            result = subprocess.run([shutil.which('cc') or 'cc', '-std=c11', '-g',
                '-I'+str(ROOT/'tests/c'), '-I'+str(ROOT/'components/esp32_mquickjs/internal'),
                str(c), str(core/'esp32_mquickjs_wireless_core.c'),
                str(core/'esp32_mquickjs_native_pool.c'), '-o', tmp+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([tmp+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_scan_failed_cancel_retains_callback_visible_storage(self):
        from test_wireless_control_regression import function, compile_run
        body = function(BLE.read_text(), 'ble_scan_destroy')
        compile_run(self, r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define BLE_HS_EALREADY 2
#define BLE_STOP_ERROR 3
typedef struct { bool transferred; uint32_t connection_generation, adapter_generation; void *ctx; } esp32_mquickjs_future_driver_state_t;
static struct { struct { bool active; uint32_t generation; void *queue,*payloads; int queue_ref,stop_reason; bool queue_rooted; } scanner; } s_ble;
static int freed, orphan, roots;
static void ble_scan_callbacks_quiesce(void) { }
static int ble_gap_disc_cancel(void) { return 1; }
static void ble_release_event_queue(void *ctx, void **q, int *r, bool *rooted) { (void)ctx; (void)q; (void)r; (void)rooted; roots++; }
static void heap_caps_free(void *p) { (void)p; freed++; }
static void ble_future_state_release(esp32_mquickjs_future_driver_state_t *s) { (void)s; }
static void ble_request_orphan_close(uint32_t generation) { (void)generation; orphan++; }
''' + body + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state = {.connection_generation=1,.adapter_generation=1};
    s_ble.scanner.active=true; s_ble.scanner.generation=1; s_ble.scanner.payloads=(void *)1;
    ble_scan_destroy(&state);
    assert(freed==0 && roots==0 && orphan==1);
    assert(s_ble.scanner.payloads==(void *)1);
}
''')

    def test_timeout_failed_stop_retains_native_storage(self):
        from test_wireless_control_regression import function, compile_run
        source = BLE.read_text()
        timeout = function(source, 'ble_future_on_timeout')
        for operation, owner, destroy, first, last in [
            ('SCAN', 'scanner', 'ble_scan_destroy', 'BLE_OP_SCAN', 'BLE_OP_ADVERTISE'),
            ('ADVERTISE', 'advertiser', 'ble_advertise_destroy', 'BLE_OP_ADVERTISE', 'BLE_OP_CONNECT'),
        ]:
            with self.subTest(operation=operation):
                # Compile the exact production timeout branch and destructor.
                branch = timeout[timeout.index('        case '+first+':'):
                                 timeout.index('        case '+last+':')]
                fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define BLE_HS_EALREADY 2
#define BLE_STOP_ERROR 3
#define BLE_OP_SCAN 1
#define BLE_OP_SCANNER_CLOSE 2
#define BLE_OP_ADVERTISE 3
#define BLE_OP_ADVERTISER_CLOSE 4
typedef struct { bool transferred; uint32_t connection_generation, adapter_generation; void *ctx; } esp32_mquickjs_future_driver_state_t;
static struct { struct { bool active; uint32_t generation; void *queue,*payloads; int queue_ref,stop_reason; bool queue_rooted; } scanner, advertiser; } s_ble;
static int freed, orphan, roots, stop_result;
static void ble_scan_callbacks_quiesce(void) { }
static void ble_advertise_callbacks_quiesce(void) { }
static void ble_discard_advertiser_connections(void *ad) { (void)ad; }
static int ble_gap_disc_cancel(void) { return stop_result; }
static int ble_gap_adv_stop(void) { return stop_result; }
static void ble_release_event_queue(void *ctx, void **q, int *r, bool *rooted) { (void)ctx; (void)q; (void)r; (void)rooted; roots++; }
static void heap_caps_free(void *p) { (void)p; freed++; }
static void ble_future_state_release(esp32_mquickjs_future_driver_state_t *s) { (void)s; }
static void ble_request_orphan_close(uint32_t generation) { (void)generation; orphan++; }
static void timeout_branch(int operation) { switch(operation) {
"""
                main = r"""
int main(void) {
    esp32_mquickjs_future_driver_state_t state = {.connection_generation=1,.adapter_generation=1};
    s_ble.OWNER.active=true; s_ble.OWNER.generation=1; s_ble.OWNER.payloads=(void *)1;
    stop_result=1;
    timeout_branch(BLE_OP_OPERATION);
    DESTROY(&state);
    assert(freed==0 && roots==0 && orphan==1);
    assert(s_ble.OWNER.active);
    stop_result=0;
    timeout_branch(BLE_OP_OPERATION);
    assert(!s_ble.OWNER.active);
    DESTROY(&state);
    assert(roots==1);
    s_ble.OWNER.active=true;
    stop_result=BLE_HS_EALREADY;
    timeout_branch(BLE_OP_OPERATION);
    assert(!s_ble.OWNER.active);
}
""".replace('OWNER', owner).replace('OPERATION', operation).replace('DESTROY', destroy)
                compile_run(self, fixture + branch + '} }\n' + function(source, destroy) + main)
