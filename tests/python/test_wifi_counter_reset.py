"""Actual counter/reset/queue paths with native boundaries; execution deferred."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from wireless_vm_fixture import ROOT, CORE, INTERNAL, build, extract, run
from wifi_connection_counter_fixture import connection_counter_code


class WiFiCounterReset(unittest.TestCase):
    def compile_native(self, code, sources=(), includes=()):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / "case.c", Path(tmp) / "case"
            source.write_text(code)
            result = subprocess.run([compiler, "-std=c11", "-pthread", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", *["-I" + str(path) for path in includes], str(source),
                *map(str, sources), "-o", str(binary)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_enqueue_receive_overflow_isr_reset_and_peak_race(self):
        source = (CORE / "esp32_mquickjs_event_queue.c").read_text()
        start = source.index("struct esp32_mquickjs_event_queue {")
        end = source.index("struct esp32_mquickjs_future_driver_state {")
        header = (INTERNAL / "esp32_mquickjs_event_queue.h").read_text()
        code = QUEUE_TYPES + source[start:end]
        for name in ("esp32_mquickjs_event_queue_status_t", "esp32_mquickjs_event_queue_stats_t"):
            code += re.search(r"typedef struct \{[^}]*\} " + name + ";", header).group(0)
        code += QUEUE_BOUNDARIES
        for name in ("event_queue_runtime", "event_queue_drain_receive", "event_queue_sum",
                     "event_queue_enqueue_send", "event_queue_enqueue_send_from_isr",
                     "esp32_mquickjs_event_queue_send", "esp32_mquickjs_event_queue_try_send_from_callback",
                     "esp32_mquickjs_event_queue_try_receive", "esp32_mquickjs_event_queue_send_from_isr",
                     "esp32_mquickjs_event_queue_discard_all", "esp32_mquickjs_event_queue_get_stats",
                     "esp32_mquickjs_get_event_queue_status", "esp32_mquickjs_reset_event_queue_counters"):
            code += extract(source, name)
        self.compile_native(code + QUEUE_MAIN, [CORE / "esp32_mquickjs_event_queue_drain.c"], [INTERNAL])

    def test_connection_reset_does_not_forget_earlier_association_or_wrap(self):
        self.compile_native(CONNECTION_BOUNDARY + connection_counter_code() + CONNECTION_MAIN)

    def test_real_manager_reset_preserves_owned_and_retired_reservations(self):
        heap = (ROOT / "tests/c/test_memory_wireless.c").read_text().split("int main(void)")[0]
        sources = [CORE / ("esp32_mquickjs_" + name + ".c") for name in
                   ("memory", "memory_budget", "memory_owner_accounting", "memory_dma_accounting")]
        self.compile_native(heap + MEMORY_MAIN, sources, [ROOT / "tests/c/memory_stubs", INTERNAL])

    def test_public_reset_arity_runtime_no_allocation_and_metadata_conversion(self):
        source = (ROOT / "components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c").read_text()
        native = re.search(r"typedef struct \{[^}]*\} wifi_diagnostics_reset_t;", source).group(0)
        native += "\nstatic wifi_diagnostics_reset_t s_diagnostics_reset;\n"
        for name in ("js_wifi_diagnostics_reset_counters", "wifi_diagnostics_reset_to_js"):
            native += extract(source, name)
        for enabled in (0, 1):
            with tempfile.TemporaryDirectory() as tmp:
                binary = build(tmp, f"#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI {enabled}\n" + RESET_BOUNDARIES + native, RESET_MAIN)
                run([str(binary)])


QUEUE_TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include "esp32_mquickjs_event_queue_resources.h"
#include "esp32_mquickjs_event_queue_drain.h"
typedef pthread_mutex_t portMUX_TYPE;
typedef int JSContext,JSValue;
typedef struct {JSValue val;} JSGCRef;
typedef struct {void *event_queue_state;} esp32_mquickjs_runtime_t;
typedef struct {uint8_t slot;uint32_t generation;} esp32_mquickjs_future_token_t;
typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;
typedef JSValue (*esp32_mquickjs_event_queue_to_js_fn)(JSContext *,const void *,void *);
typedef void (*esp32_mquickjs_event_queue_drop_fn)(void *,void *);
typedef void (*esp32_mquickjs_event_queue_close_fn)(void *);
typedef enum {ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST} esp32_mquickjs_event_queue_overflow_t;
'''
QUEUE_BOUNDARIES = r'''
static _Thread_local unsigned critical;
#define portENTER_CRITICAL(p) do {assert(!critical);assert(!pthread_mutex_lock(p));++critical;} while(0)
#define portEXIT_CRITICAL(p) do {assert(critical==1);--critical;assert(!pthread_mutex_unlock(p));} while(0)
#define portENTER_CRITICAL_ISR portENTER_CRITICAL
#define portEXIT_CRITICAL_ISR portEXIT_CRITICAL
typedef struct {unsigned count;int values[3];} queue_boundary_t;
typedef queue_boundary_t *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 99
static unsigned freed,wakes;
static pthread_mutex_t race_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_changed=PTHREAD_COND_INITIALIZER;
static bool race,submitted;
static int xSemaphoreTake(void *p,int wait){assert(p && wait==portMAX_DELAY && !critical);return pdTRUE;}
static int xSemaphoreGive(void *p){assert(p&&!critical);return pdTRUE;}
static int xQueueSend(QueueHandle_t q,const void *event,int wait){
    assert(critical==1&&!wait);if(q->count==3)return pdFALSE;
    q->values[q->count++]=*(const int *)event;
    if(race){pthread_mutex_lock(&race_lock);submitted=true;pthread_cond_signal(&race_changed);pthread_mutex_unlock(&race_lock);}
    return pdTRUE;
}
static int xQueueSendFromISR(QueueHandle_t q,const void *event,int *woken){*woken=0;return xQueueSend(q,event,0);}
static int xQueueReceive(QueueHandle_t q,void *event,int wait){
    assert(critical==1&&!wait);if(!q->count)return pdFALSE;
    *(int *)event=q->values[0];--q->count;memmove(q->values,q->values+1,q->count*sizeof(int));return pdTRUE;
}
static unsigned uxQueueMessagesWaitingFromISR(QueueHandle_t q){assert(critical==1);return q->count;}
static void event_queue_wake_receiver(esp32_mquickjs_event_queue_t *q){assert(q&&!critical);++wakes;}
static bool esp32_mquickjs_future_wake_from_isr(void *r,esp32_mquickjs_future_token_t t,int *woken){(void)r;(void)t;assert(!critical);*woken=0;++wakes;return true;}
static void drop(void *event,void *opaque){(void)opaque;assert(!critical&&*(int *)event>0);++freed;}
'''
QUEUE_MAIN = r'''
static void *consumer(void *opaque){
    pthread_mutex_lock(&race_lock);while(!submitted)pthread_cond_wait(&race_changed,&race_lock);pthread_mutex_unlock(&race_lock);
    int event;assert(esp32_mquickjs_event_queue_try_receive(opaque,&event)&&event==7);return NULL;
}
int main(void){
    queue_boundary_t native={0};int scratch=0,drain=0;
    esp32_mquickjs_event_queue_t q={.resources={.events=&native,.send_lock=&native,.overflow_scratch=&scratch,.drain_scratch=&drain},.capacity=3,.drop=drop};
    assert(!pthread_mutex_init(&q.lock,NULL));
    esp32_mquickjs_event_queue_runtime_t registry={.head=&q};
    esp32_mquickjs_runtime_t runtime={.event_queue_state=&registry};q.runtime=&runtime;
    int event=1,woken=0;
    assert(esp32_mquickjs_event_queue_send(&q,&event));event=2;
    assert(esp32_mquickjs_event_queue_try_send_from_callback(&q,&event));event=3;
    assert(esp32_mquickjs_event_queue_send_from_isr(&q,&event,&woken));
    esp32_mquickjs_event_queue_stats_t stats;assert(esp32_mquickjs_event_queue_get_stats(&q,&stats));
    assert(stats.queued==3&&stats.high_water==3&&!stats.dropped);
    event=4;assert(!esp32_mquickjs_event_queue_send(&q,&event)&&q.dropped==1);
    q.overflow=ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST;
    assert(esp32_mquickjs_event_queue_send(&q,&event)&&freed==1&&q.dropped==2);
    int value;assert(esp32_mquickjs_event_queue_try_receive(&q,&value)&&value==2);
    q.receiver_registered=true;q.receiver=(esp32_mquickjs_future_token_t){2,19};q.native_retain_count=4;
    esp32_mquickjs_reset_event_queue_counters(&runtime);
    assert(q.high_water==2&&!q.dropped&&native.count==2&&q.receiver_registered&&q.receiver.generation==19&&q.native_retain_count==4);
    q.dropped=UINT32_MAX;event=5;assert(esp32_mquickjs_event_queue_send(&q,&event));event=6;
    assert(esp32_mquickjs_event_queue_send(&q,&event)&&q.dropped==UINT32_MAX);
    esp32_mquickjs_event_queue_status_t total;assert(esp32_mquickjs_get_event_queue_status(&runtime,&total));
    assert(total.dropped==UINT32_MAX&&total.high_water==3&&total.queued==3&&total.open==1);
    assert(esp32_mquickjs_event_queue_discard_all(&q)==3&&native.count==0);
    esp32_mquickjs_reset_event_queue_counters(&runtime);assert(!q.high_water);
    race=true;pthread_t reader;assert(!pthread_create(&reader,NULL,consumer,&q));event=7;
    assert(esp32_mquickjs_event_queue_send(&q,&event));assert(!pthread_join(reader,NULL));
    assert(esp32_mquickjs_event_queue_get_stats(&q,&stats)&&stats.queued==0&&stats.high_water==1);
    atomic_store(&q.closed,true);unsigned prior=wakes;assert(!esp32_mquickjs_event_queue_send(&q,&event)&&wakes==prior);
    esp32_mquickjs_reset_event_queue_counters(&runtime);assert(atomic_load(&q.closed)&&q.receiver_registered&&q.native_retain_count==4);
    assert(!critical);pthread_mutex_destroy(&q.lock);
}
'''
CONNECTION_BOUNDARY = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked;
#define portENTER_CRITICAL(p) do {(void)(p);assert(!locked);++locked;} while(0)
#define portEXIT_CRITICAL(p) do {(void)(p);assert(locked==1);--locked;} while(0)
'''
CONNECTION_MAIN = r'''
int main(void){
    wifi_connection_note_submit();wifi_connection_note_failure();
    assert(s_wifi_connection_counters.attempts==1&&!s_wifi_connection_counters.reconnect_attempts&&s_wifi_connection_counters.submission_failures==1);
    wifi_connection_note_association();wifi_connection_note_submit();
    assert(s_wifi_connection_counters.reconnect_attempts==1&&s_wifi_connection_counters.associations==1);
    esp32_mquickjs_wifi_reset_connection_counters();
    assert(s_wifi_connection_counters.ever_associated&&!s_wifi_connection_counters.attempts&&!s_wifi_connection_counters.associations);
    wifi_connection_note_submit();assert(s_wifi_connection_counters.reconnect_attempts==1);
    s_wifi_connection_counters.attempts=UINT32_MAX;s_wifi_connection_counters.reconnect_attempts=UINT32_MAX;
    wifi_connection_note_submit();assert(s_wifi_connection_counters.attempts==UINT32_MAX&&s_wifi_connection_counters.reconnect_attempts==UINT32_MAX&&!locked);
}
'''
MEMORY_MAIN = r'''
int main(void){
    psram=true;esp32_mquickjs_memory_init();
    void *pool=esp32_mquickjs_memory_wireless_alloc("wifi",64,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(pool);
    assert(esp32_mquickjs_memory_wireless_retire(pool));
    void *control=esp32_mquickjs_memory_wireless_alloc("wifi",32,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);assert(control);
    assert(!esp32_mquickjs_memory_wireless_alloc("wifi",100000,ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL));
    esp32_mquickjs_memory_status_t before=status();assert(before.allocation_failures&&before.wireless.rejected);
    assert(before.wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL]==64);
    assert(before.wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL]==32);
    unsigned calls=allocation_calls;
    esp32_mquickjs_memory_reset_counters();esp32_mquickjs_memory_status_t after=status();
    assert(calls==allocation_calls&&!after.allocation_failures&&!after.wireless.rejected);
    assert(before.managed_internal_bytes==after.managed_internal_bytes&&before.managed_psram_bytes==after.managed_psram_bytes);
    assert(before.allocation_count==after.allocation_count&&!memcmp(before.allocations,after.allocations,sizeof(before.allocations)));
    for(unsigned i=0;i<2;i++){
        assert(after.wireless.regions[i].high_water==after.wireless.regions[i].reserved);
        assert(before.wireless.regions[i].reserved==after.wireless.regions[i].reserved);
        assert(!memcmp(before.wireless.regions[i].roles,after.wireless.regions[i].roles,sizeof(before.wireless.regions[i].roles)));
    }
    esp32_mquickjs_memory_payload_free(pool);esp32_mquickjs_memory_payload_free(control);empty();
}
'''
RESET_BOUNDARIES = r'''
typedef int esp32_mquickjs_runtime_t;
static esp32_mquickjs_runtime_t runtime;
static bool active=true;static unsigned providers;static int64_t clock_us;
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void){return active?&runtime:NULL;}
static int64_t esp_timer_get_time(void){return ++clock_us;}
static void esp32_mquickjs_reset_event_queue_counters(void *r){assert(r==&runtime);providers++;}
static void esp32_mquickjs_wifi_watch_reset_counters(void){providers++;}
static void esp32_mquickjs_wifi_reset_connection_counters(void){providers++;}
static uint32_t esp32_mquickjs_wifi_monitor_reset_counters(void){providers++;return 2;}
static uint32_t esp32_mquickjs_wifi_csi_reset_counters(void){providers++;return 3;}
static void esp32_mquickjs_memory_reset_counters(void){providers++;}
'''
RESET_MAIN = r'''
int main(void){
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    assert(JS_IsException(js_wifi_diagnostics_reset_counters(ctx,NULL,1,NULL)));JS_GetException(ctx);assert(!providers);
    active=false;assert(JS_IsException(js_wifi_diagnostics_reset_counters(ctx,NULL,0,NULL)));JS_GetException(ctx);assert(!providers);active=true;
    inject=true;calls=0;fail_at=1;
    assert(JS_IsUndefined(js_wifi_diagnostics_reset_counters(ctx,NULL,0,NULL))&&!calls);
    inject=false;assert(providers==5+CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI);
    assert(s_diagnostics_reset.count==1&&s_diagnostics_reset.started_us==1&&s_diagnostics_reset.finished_us==2);
    assert(s_diagnostics_reset.unavailable_monitor_generations==2);
    assert(s_diagnostics_reset.unavailable_csi_generations==3*CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI);
    JSGCRef ref;JSValue *r=JS_PushGCRef(ctx,&ref);int total=0;
    for(int nth=0;nth<=total;nth++){
        inject=true;collect=true;calls=0;fail_at=nth;*r=wifi_diagnostics_reset_to_js(ctx,&s_diagnostics_reset);
        inject=false;collect=false;
        if(!nth){total=calls;assert(!JS_IsException(*r));}else {assert(JS_IsException(*r));JS_GetException(ctx);}
        assert(providers==5+CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI&&s_diagnostics_reset.count==1);
    }
    JS_PopGCRef(ctx,&ref);assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
}
'''
