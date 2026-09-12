"""Run production wait/watchdog hooks with runtime and worker task identities."""
from pathlib import Path
import unittest
from wireless_vm_fixture import extract
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs.c'
HOST = ROOT / 'components/esp32qjs_runtime/src/esp32qjs_runtime.c'

BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef void *TaskHandle_t;
typedef struct { void *task_handle; } esp32_mquickjs_async_state_t;
typedef struct {
    uint64_t deadline_us, scoped_deadline_us;
    uint16_t native_wait_depth;
    bool (*cooperate)(void *);
    void *cooperate_opaque;
    esp32_mquickjs_async_state_t *async;
} esp32_mquickjs_runtime_t;
typedef struct {uint64_t saved_deadline_us,started_us;bool active;} esp32_mquickjs_native_wait_t;
typedef struct {
    esp32_mquickjs_runtime_t engine;
    TaskHandle_t task;
    bool watchdog_registered, js_watchdog_registered, stop_requested;
    void *js_watchdog_user;
    uint64_t last_outer_heartbeat_us;
} esp32qjs_runtime_t;
static TaskHandle_t current;
static uint64_t now;
static bool due;
static unsigned task_feeds, foreign_feeds, js_feeds;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) {return current;}
static int64_t esp_timer_get_time(void) {return now;}
static esp32_mquickjs_async_state_t *esp32_mquickjs_async_state(esp32_mquickjs_runtime_t *r) {return r?r->async:NULL;}
static bool runtime_control_due(esp32qjs_runtime_t *r) {(void)r;return due;}
static int esp_task_wdt_reset(void) {
    if (current!=(void *)1) foreign_feeds++;
    else task_feeds++;
    return 0;
}
static int esp_task_wdt_reset_user(void *user) {assert(user==(void *)3);js_feeds++;return 0;}
static void runtime_feed_js_watchdog(esp32qjs_runtime_t *runtime);
'''

MAIN = r'''
int main(void) {
    esp32_mquickjs_async_state_t async={.task_handle=(void *)1};
    esp32qjs_runtime_t host={.task=(void *)1,.watchdog_registered=true,
        .js_watchdog_registered=true,.js_watchdog_user=(void *)3};
    esp32_mquickjs_runtime_t *engine=&host.engine;
    engine->async=&async;engine->cooperate=runtime_cooperate;engine->cooperate_opaque=&host;
    engine->deadline_us=1000;engine->native_wait_depth=1;
    current=(void *)2;now=100;
#if TEST_WAIT
    esp32_mquickjs_native_wait_t worker;
    esp32_mquickjs_native_wait_begin(engine,&worker);
    assert(!worker.active && engine->deadline_us==1000 && engine->native_wait_depth==1);
    now=150;esp32_mquickjs_native_wait_end(engine,&worker);
    assert(engine->deadline_us==1000 && engine->native_wait_depth==1);
    /* Worker cancellation follows runtime control, never another JS exec's deadline. */
    engine->scoped_deadline_us=50;
    assert(esp32_mquickjs_cooperate(engine));
    assert(!task_feeds && !foreign_feeds && !js_feeds);
    current=(void *)1;assert(!esp32_mquickjs_cooperate(engine));
    engine->scoped_deadline_us=0;engine->native_wait_depth=0;
    esp32_mquickjs_native_wait_t outer,inner;
    now=200;esp32_mquickjs_native_wait_begin(engine,&outer);
    assert(outer.active && !engine->deadline_us && engine->native_wait_depth==1);
    now=220;esp32_mquickjs_native_wait_begin(engine,&inner);
    now=230;esp32_mquickjs_native_wait_end(engine,&inner);
    assert(!engine->deadline_us && engine->native_wait_depth==1);
    now=260;esp32_mquickjs_native_wait_end(engine,&outer);
    assert(engine->deadline_us==1060 && !engine->native_wait_depth);
    esp32_mquickjs_native_wait_end(engine,&outer);
    assert(engine->deadline_us==1060 && !engine->native_wait_depth);
    async.task_handle=NULL;
    esp32_mquickjs_native_wait_begin(engine,&worker);
    assert(!worker.active && engine->deadline_us==1060 && !engine->native_wait_depth);
#else
    assert(runtime_cooperate(&host));
    assert(!task_feeds && !foreign_feeds && !js_feeds && !host.last_outer_heartbeat_us);
    due=true;assert(!runtime_cooperate(&host));due=false;
    host.stop_requested=true;assert(!runtime_cooperate(&host));host.stop_requested=false;
    current=(void *)1;engine->native_wait_depth=0;
    assert(runtime_cooperate(&host));assert(task_feeds==1 && !js_feeds);
    engine->native_wait_depth=1;
    assert(runtime_cooperate(&host));assert(task_feeds==2 && js_feeds==1 && host.last_outer_heartbeat_us==100);
    assert(!foreign_feeds);
    host.watchdog_registered=host.js_watchdog_registered=false;
    assert(runtime_cooperate(&host));assert(task_feeds==2 && js_feeds==1);
    assert(!runtime_cooperate(NULL));
#endif
    return 0;
}
'''

class RuntimeWaitTaskTests(unittest.TestCase):
    def test_worker_cannot_change_js_wait_or_deadline(self):
        self.run_case(True)

    def test_only_runtime_task_feeds_its_watchdogs(self):
        self.run_case(False)

    def run_case(self, wait):
        core, host = CORE.read_text(), HOST.read_text()
        code = BOUNDARIES
        if 'static bool esp32_mquickjs_runtime_task_is_current(' in core:
            code += extract(core, 'esp32_mquickjs_runtime_task_is_current')
        for name in ('runtime_cooperate','runtime_feed_js_watchdog'):
            code += extract(host,name)
        for name in ('esp32_mquickjs_cooperate','esp32_mquickjs_native_wait_begin','esp32_mquickjs_native_wait_end'):
            code += extract(core,name)
        compile_run(self, code+'\n#define TEST_WAIT '+str(int(wait))+'\n'+MAIN)
