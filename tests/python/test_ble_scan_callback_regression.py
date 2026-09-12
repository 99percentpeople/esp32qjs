"""Execute production scanner submission/cleanup with controlled SDK callbacks."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from test_wireless_control_regression import function, compile_run

ROOT = pathlib.Path(__file__).resolve().parents[2]
BLE = ROOT / 'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'

FIXTURE = r'''
#define esp32_mquickjs_memory_payload_free heap_caps_free
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#ifdef THREADED
#include <pthread.h>
#include <sched.h>
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t schedule_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t schedule_cond=PTHREAD_COND_INITIALIZER;
static bool entered, resume_callback;
#endif
#define BLE_LIFECYCLE_ACTIVE 1
#define BLE_HS_EINVAL 2
#define BLE_HS_EBUSY 3
#define BLE_STOP_ERROR 4
#define BLE_GAP_EVENT_DISC_COMPLETE 5
#define BLE_GAP_EVENT_DISC 6
typedef void JSContext;
typedef void esp32_mquickjs_runtime_t;
typedef int esp32_mquickjs_future_token_t;
typedef struct { int generation, stop_reason, queue_ref; bool active, queue_rooted; void *queue, *payloads; int64_t started_at_us; } ble_scanner_t;
typedef struct { int lifecycle, generation, own_addr_type, lock; JSContext *ctx; ble_scanner_t scanner; } ble_adapter_t;
typedef struct { int adapter_generation, connection_generation, token, duration_ms, scan_params, host_code; void *runtime; bool started; atomic_bool completed; } esp32_mquickjs_future_driver_state_t;
static ble_adapter_t s_ble = {.lifecycle=BLE_LIFECYCLE_ACTIVE, .generation=1};
struct ble_gap_event { int type; };
typedef int (*callback_t)(struct ble_gap_event *, void *);
static callback_t saved_callback;
static void *saved_arg;
static int immediate, driver_result, driver_calls, delivered, freed, wakes;
static uintptr_t s_ble_scan_next_identity=1, s_ble_scan_callback_identity;
static uint32_t s_ble_scan_callback_refs;
static _Thread_local int lock_depth;
#ifdef THREADED
#define taskENTER_CRITICAL(lock) do { (void)(lock); pthread_mutex_lock(&state_lock); assert(lock_depth++ == 0); } while (0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock); assert(--lock_depth == 0); pthread_mutex_unlock(&state_lock); } while (0)
#else
#define taskENTER_CRITICAL(lock) do { (void)(lock); assert(lock_depth++ == 0); } while (0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock); assert(--lock_depth == 0); } while (0)
#endif
static void vTaskDelay(int ticks) {
    (void)ticks; assert(!lock_depth);
#ifdef THREADED
    assert(freed==0);
    pthread_mutex_lock(&schedule_lock);
    resume_callback=true;
    pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    sched_yield();
#endif
}
static int ble_gap_event_callback(struct ble_gap_event *event, void *arg) {
    (void)arg;
    assert(!lock_depth);
#ifdef THREADED
    pthread_mutex_lock(&schedule_lock);
    entered=true;
    pthread_cond_broadcast(&schedule_cond);
    while (!resume_callback) pthread_cond_wait(&schedule_cond,&schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
#endif
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) s_ble.scanner.active=false;
    else if (s_ble.scanner.active) delivered++;
    return 0;
}
static int ble_gap_disc(int addr, int duration, const int *params, callback_t cb, void *arg) {
    (void)addr; (void)duration; (void)params;
    assert(!lock_depth);
    driver_calls++;
    saved_callback=cb; saved_arg=arg;
    if (immediate) { struct ble_gap_event event={BLE_GAP_EVENT_DISC_COMPLETE}; cb(&event,arg); }
    return driver_result;
}
static void ble_throw_error(void *ctx, const char *code, int rc, int a, int b, int c) { (void)ctx; (void)code; (void)rc; (void)a; (void)b; (void)c; }
static int64_t esp_timer_get_time(void) { return 1; }
static bool esp32_mquickjs_future_wake(void *runtime, int token) { (void)runtime; (void)token; wakes++; return true; }
static void ble_release_event_queue(void *ctx, void **q, int *ref, bool *rooted) { (void)ctx; (void)q; (void)ref; (void)rooted; }
static void heap_caps_free(void *ptr) { assert(s_ble_scan_callback_refs==0); if (ptr) freed++; }
'''
MAIN = r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.adapter_generation=1,.connection_generation=1};
    s_ble.scanner.generation=1;
    s_ble.scanner.payloads=(void *)1;
    immediate=IMMEDIATE;
    assert(ble_scan_start(NULL,NULL,1,&state));
    if (immediate) { assert(!s_ble.scanner.active); return 0; }
    callback_t old_callback=saved_callback;
    void *old_arg=saved_arg;
    /* SDK has already copied cb/arg, but has not entered it when cancel succeeds. */
    s_ble.scanner.active=false;
    ble_release_scanner(&s_ble);
    assert(freed==1);
    s_ble.scanner.generation=2;
    state.connection_generation=2;
    assert(ble_scan_start(NULL,NULL,2,&state));
    struct ble_gap_event report={BLE_GAP_EVENT_DISC};
    old_callback(&report,old_arg);
    assert(delivered==0);
    struct ble_gap_event complete={BLE_GAP_EVENT_DISC_COMPLETE};
    old_callback(&complete,old_arg);
    assert(s_ble.scanner.active);
    saved_callback(&report,saved_arg);
    assert(delivered==1);
    saved_callback(&complete,saved_arg);
    assert(!s_ble.scanner.active);
}
'''

class BleScanCallbackRegression(unittest.TestCase):
    def production(self):
        source = BLE.read_text()
        return '\n'.join(function(source, name) for name in (
            'ble_scan_callback_bind', 'ble_scan_event_callback', 'ble_scan_callbacks_quiesce',
            'ble_release_scanner', 'ble_scan_start'))

    def test_close_waits_for_entered_callback_before_releasing_storage(self):
        import shutil
        import subprocess
        import tempfile
        main = r'''
static void *callback_thread(void *unused) {
    (void)unused;
    struct ble_gap_event event={BLE_GAP_EVENT_DISC};
    saved_callback(&event,saved_arg);
    return NULL;
}
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.adapter_generation=1,.connection_generation=1};
    s_ble.scanner.generation=1; s_ble.scanner.payloads=(void *)1;
    assert(ble_scan_start(NULL,NULL,1,&state));
    pthread_t thread;
    assert(!pthread_create(&thread,NULL,callback_thread,NULL));
    pthread_mutex_lock(&schedule_lock);
    while (!entered) pthread_cond_wait(&schedule_cond,&schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    assert(ble_scan_callback_bind()==NULL);
    /* The SDK callback is inside the adapter when the runtime closes it. */
    ble_release_scanner(&s_ble);
    assert(!pthread_join(thread,NULL));
    assert(freed==1 && delivered==1 && s_ble_scan_callback_identity==0);
    assert(s_ble_scan_callback_refs==0);
    struct ble_gap_event event={BLE_GAP_EVENT_DISC_COMPLETE};
    saved_callback(&event,saved_arg);
    assert(ble_scan_callback_bind()!=NULL);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            source = pathlib.Path(temporary) / 'race.c'
            source.write_text('#define THREADED\n' + FIXTURE + self.production() + main)
            result = subprocess.run([shutil.which('cc') or 'cc', '-std=c11', '-pthread',
                                     str(source), '-o', temporary+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([temporary+'/test'], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_cookie_exhaustion_fails_without_driver_call_or_reuse(self):
        compile_run(self, FIXTURE + self.production() + r'''
int main(void) {
    s_ble_scan_next_identity=UINTPTR_MAX;
    esp32_mquickjs_future_driver_state_t state={.adapter_generation=1,.connection_generation=1};
    s_ble.scanner.generation=1;
    assert(ble_scan_start(NULL,NULL,1,&state));
    assert((uintptr_t)saved_arg==UINTPTR_MAX);
    ble_release_scanner(&s_ble);
    s_ble.scanner.generation=1;
    assert(!ble_scan_start(NULL,NULL,1,&state));
    assert(driver_calls==1 && s_ble_scan_next_identity==0);
}
''')

    def test_failed_submission_retires_cookie_and_allows_next_scan(self):
        compile_run(self, FIXTURE + self.production() + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.adapter_generation=1,.connection_generation=1};
    s_ble.scanner.generation=1;
    driver_result=BLE_HS_EBUSY;
    assert(!ble_scan_start(NULL,NULL,1,&state));
    void *old_arg=saved_arg;
    assert(!s_ble.scanner.active && s_ble_scan_callback_identity==0);
    driver_result=0;
    assert(ble_scan_start(NULL,NULL,1,&state));
    assert(saved_arg!=old_arg);
    struct ble_gap_event event={BLE_GAP_EVENT_DISC_COMPLETE};
    saved_callback(&event,old_arg);
    assert(s_ble.scanner.active);
}
''')

    def test_completion_during_submission_is_not_resurrected(self):
        self.run_schedule(1)

    def test_callback_copied_before_cancel_cannot_touch_reopened_scan(self):
        self.run_schedule(0)

    def run_schedule(self, immediate):
        compile_run(self, FIXTURE + self.production() + MAIN.replace('IMMEDIATE', str(immediate)))
