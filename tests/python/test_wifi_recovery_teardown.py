"""Deferred real runtime destruction, Future prepare and recovery cleanup order.

Composes production core destruction, Future slot teardown, shared recovery poll/
cancel/dispose and runtime cleanup. VM resources, unrelated native Future and
Radio/worker completion are injected boundaries. Native physical proof itself is
covered by Action/FTM Radio fixtures. No fixture import/compile/run in API phase.
"""
import re
import unittest
from test_wifi_recovery_runtime import recovery_runtime_code
from test_wifi_recovery_future import BOUNDARIES as CLOCK
from test_wifi_configuration_cleanup import WIFI
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, INTERNAL, extract


def teardown_code():
    code = '#define esp32_mquickjs_memory_payload_free heap_caps_free\n' + recovery_runtime_code(1)
    future = (CORE / 'esp32_mquickjs_future.c').read_text()
    recovery = (WIFI.parent.parent / 'wifi_common/esp32_mquickjs_wifi_recovery.c').read_text()
    header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
    for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += '\n' + re.search(r'^#define ESP32_MQUICKJS_FUTURE_DRIVER_CHUNK_SIZE .*$', future, re.M).group(0) + '\n'
    code += TYPES
    start = recovery.index('struct esp32_mquickjs_future_driver_state {')
    code += recovery[start:recovery.index('\n};', start) + 3]
    code += CLOCK
    code += ''.join(extract(recovery, name) for name in ('recovery_start', 'recovery_poll', 'recovery_cancel'))
    start = future.index('typedef enum {\n    FUTURE_STATE_QUEUED,')
    code += future[start:future.index('} future_runtime_t;', start) + len('} future_runtime_t;')]
    code += BOUNDARIES
    for name in ('future_runtime', 'future_is_terminal', 'future_clear_driver_registry', 'future_release_call_refs',
                 'future_release_input_refs', 'future_stop_timer', 'future_destroy_driver', 'future_clear_slot'):
        code += extract(future, name)
    code += extract((CORE / 'esp32_mquickjs_future_scheduler.c').read_text(),
                    'esp32_mquickjs_future_scheduler_teardown_must_wait')
    code += extract(future, 'esp32_mquickjs_prepare_future_runtime_destroy')
    code += extract(WIFI.read_text(), 'esp32_mquickjs_prepare_wifi_recovery_runtime_destroy')
    code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_destroy_internal')
    return code


class WiFiRecoveryTeardown(unittest.TestCase):
    def test_cancelled_recovery_progresses_while_other_future_or_original_owner_waits(self):
        compile_run(self, teardown_code() + MAIN)


TYPES = r'''
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define ESP32_MQUICKJS_FUTURE_SLOT_COUNT 2
#define ESP32_MQUICKJS_FUTURE_MAX_DRIVERS 2
/* VM/queue storage layout boundary; production code owns all transitions. */
typedef int JSContext,JSValue,JSGCRef,esp32_mquickjs_future_token_t;
typedef void *QueueHandle_t,*esp_timer_handle_t;
typedef const void *esp32_mquickjs_resource_key_t;
typedef int esp32_mquickjs_future_runtime_resources_t;
typedef struct {
    void *future_state,*startup_bytecode,*prepare_output,*prepare_output_opaque,*cooperate,*cooperate_opaque;
    int64_t deadline_us,scoped_deadline_us,context_started_us;
    unsigned native_wait_depth,load_root_depth;
    bool littlefs_mounted,repl_enabled,auto_run_startup_script,format_littlefs_on_mount_fail;
    char startup_fs_root[4],load_root[4];
} esp32_mquickjs_runtime_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
typedef struct {
    esp32_mquickjs_cancel_result_t (*cancel)(esp32_mquickjs_future_driver_state_t *);
    esp32_mquickjs_future_poll_t (*poll)(esp32_mquickjs_future_driver_state_t *);
    void (*destroy)(esp32_mquickjs_future_driver_state_t *);
} esp32_mquickjs_future_driver_t;
'''

BOUNDARIES = r'''
static void *registry_storage;
static void future_runtime_resource_release(void *p,void *opaque) {assert(p==registry_storage && !opaque);registry_storage=NULL;}
static bool esp32_mquickjs_wifi_smartconfig_poll_observations(bool teardown) {assert(teardown);return false;}
static bool esp32_mquickjs_wifi_wps_poll_observations(bool teardown) {assert(teardown);return false;}
static bool esp32_mquickjs_wifi_wps_prepare_runtime_destroy(void) {return true;}
static bool hold_future,hold_worker,hold_raw,hold_enterprise,hold_smartconfig;
static unsigned enterprise_prepares;
static unsigned smartconfig_prepares;
static bool esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy(void) {++smartconfig_prepares;return !hold_smartconfig;}
static bool esp32_mquickjs_wifi_eap_prepare_runtime_destroy(void) {++enterprise_prepares;return !hold_enterprise;}
static unsigned vm_frees,root_deletes,recovery_disposals,action_prepares,raw_prepares,ftm_prepares;
static esp32_mquickjs_runtime_t *s_active_runtime;
static void JS_DeleteGCRef(JSContext *ctx,JSGCRef *ref) {(void)ctx;(void)ref;++root_deletes;}
static int esp_timer_stop(void *timer) {(void)timer;return 0;}
static int esp_timer_delete(void *timer) {(void)timer;return 0;}
/* Allocation is outside capture here. The actual caller-owned recovery snapshot
 * is disposed by production code; native operation storage remains independent. */
static void dispose_recovery(esp32_mquickjs_future_driver_state_t *state) {
    assert(state->cancelled);esp32_mquickjs_wifi_recovery_dispose(&state->recovery);++recovery_disposals;
}
static const esp32_mquickjs_future_driver_t recovery_driver={recovery_cancel,recovery_poll,dispose_recovery};
/* An unrelated native driver can remain pending while cleanup must advance. */
static esp32_mquickjs_cancel_result_t pending_cancel(esp32_mquickjs_future_driver_state_t *state) {
    (void)state;return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static esp32_mquickjs_future_poll_t pending_poll(esp32_mquickjs_future_driver_state_t *state) {
    (void)state;return hold_future?ESP32_MQUICKJS_FUTURE_PENDING:ESP32_MQUICKJS_FUTURE_READY;
}
static const esp32_mquickjs_future_driver_t pending_driver={pending_cancel,pending_poll,NULL};
static bool esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(esp32_mquickjs_runtime_t *runtime) {(void)runtime;return true;}
static void consume_original(esp32_mquickjs_wifi_recovery_kind_t kind) {
    /* Radio boundary reports post-deinit pending retirement, worker completes
     * only when explicitly scheduled. Never infer termination from STOP alone. */
    if(original.kind==kind && !hold_worker && recovery_shutdown_calls)
        recovery_owner_pending=false;
}
static bool esp32_mquickjs_prepare_wifi_action_runtime_destroy(void) {
    ++action_prepares;consume_original(ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
    return original.kind!=ESP32_MQUICKJS_WIFI_RECOVERY_ACTION || !recovery_owner_pending;
}
static bool esp32_mquickjs_prepare_wifi_raw_tx_runtime_destroy(void) {
    ++raw_prepares;consume_original(ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
    return !hold_raw && (original.kind!=ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX || !recovery_owner_pending);
}
static bool esp32_mquickjs_wifi_ftm_prepare_runtime_destroy(void) {
    ++ftm_prepares;consume_original(ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
    return original.kind!=ESP32_MQUICKJS_WIFI_RECOVERY_FTM || !recovery_owner_pending;
}
static int esp32_mquickjs_poll_reapers(esp32_mquickjs_runtime_t *r) {(void)r;return 0;}
static int esp32_mquickjs_reapers_pending(esp32_mquickjs_runtime_t *r) {(void)r;return 0;}
static void esp32_mquickjs_clear_idle_jobs(JSContext *c,esp32_mquickjs_runtime_t *r) {(void)c;(void)r;}
static void esp32_mquickjs_deinit_time_runtime(void) {}
static void esp32_mquickjs_deinit_stream_runtime(void) {}
static void esp32_mquickjs_deinit_wifi_runtime(JSContext *ctx) {
    (void)ctx;assert(!s_wifi_configuration_cleanup && !recovery_owner_pending);
}
static void esp32_mquickjs_deinit_timer_state(JSContext *c,esp32_mquickjs_runtime_t *r) {(void)c;(void)r;}
static void JS_FreeContext(JSContext *ctx) {(void)ctx;++vm_frees;}
void esp32_mquickjs_memory_release_generation(void) {}
static void esp32_mquickjs_deinit_event_queue_runtime(esp32_mquickjs_runtime_t *r) {(void)r;}
static void teardown_payload_free(void *p) {assert(!p);}
#undef esp32_mquickjs_memory_payload_free
#define esp32_mquickjs_memory_payload_free teardown_payload_free
static void esp32_mquickjs_deinit_future_runtime(esp32_mquickjs_runtime_t *r) {r->future_state=NULL;}
static void esp32_mquickjs_deinit_async_state(esp32_mquickjs_runtime_t *r) {(void)r;}
'''

MAIN = r'''
int main(void) {
    for(int kind=0;kind<3;++kind) for(unsigned point=0;point<6;++point) {
        setup_restart();original.kind=kind;recovery_mode=WIFI_MODE_STA;
        recovery_admitted=false;recovery_owner_pending=true;recovery_shutdown_calls=0;
        hold_future=hold_worker=hold_raw=hold_enterprise=hold_smartconfig=true;vm_frees=root_deletes=recovery_disposals=0;
        smartconfig_prepares=0;
        action_prepares=raw_prepares=ftm_prepares=0;
        esp32_mquickjs_future_driver_state_t public_state={.operation=original,.timeout_ms=1000};
        assert(!esp32_mquickjs_wifi_recovery_begin(&public_state.recovery,&original,false));
        bool complete=false;
        for(unsigned i=0;i<point;++i)
            assert(!esp32_mquickjs_wifi_recovery_step(&public_state.recovery,&complete) && !complete);
        future_slot_t slots[2]={
            {.allocated=true,.driver_active=true,.state=FUTURE_STATE_PENDING,.driver=&recovery_driver,.driver_state=&public_state},
            {.allocated=true,.driver_active=true,.state=FUTURE_STATE_PENDING,.driver=&pending_driver},
        };
        future_driver_chunk_t drivers={.count=1,.entries={{.retained=true}}};
        future_runtime_t future={.slots=slots,.driver_count=1,.drivers=&drivers};registry_storage=&drivers;
        esp32_mquickjs_runtime_t runtime={.future_state=&future};JSContext ctx=0;s_active_runtime=&runtime;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(future.shutting_down && recovery_disposals==1 && !temporary && !slots[0].allocated);
        /* Before repair: core returned after Future prepare, never reaching this cleanup. */
        assert(!running && recovery_shutdown_calls && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity);
        assert(action_prepares==1 && raw_prepares==1 && ftm_prepares==1);
        assert(smartconfig_prepares==1); /* Native cleanup advances while Future is pending. */
        assert(!vm_frees && !root_deletes && slots[1].allocated && s_active_runtime==&runtime);
        unsigned stopped=stop_calls;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(stop_calls==stopped && recovery_owner_pending && !vm_frees);
        hold_worker=false;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!recovery_owner_pending && s_wifi_configuration_cleanup && !vm_frees);
        /* Owner retirement permits central suffix, even while another Future waits. */
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !vm_frees);
        assert(!phase_calls[2] && !phase_calls[5] && !phase_calls[9]); /* no restoration */
        hold_future=false;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!vm_frees && root_deletes==1 && !future.driver_count && !slots[1].allocated);
        hold_raw=false;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false) && !vm_frees && enterprise_prepares);
        hold_enterprise=false;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false) && !vm_frees && smartconfig_prepares);
        hold_smartconfig=false;
        assert(esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(vm_frees==1 && !s_active_runtime && !runtime.future_state && stop_calls==stopped);
    }
    for(int kind=0;kind<3;++kind) for(int failure=1;failure<=4;++failure) {
        setup_restart();original.kind=kind;recovery_mode=WIFI_MODE_APSTA;
        s_ap_netif=(void *)1;s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
        recovery_admitted=false;recovery_owner_pending=true;recovery_shutdown_calls=0;
        hold_future=hold_raw=false;hold_worker=true;vm_frees=0;
        esp32_mquickjs_future_driver_state_t public_state={.operation=original,.timeout_ms=1000};
        assert(!esp32_mquickjs_wifi_recovery_begin(&public_state.recovery,&original,false));
        /* Inject an already completed original-owner boundary for final lifecycle
         * failure; otherwise hold it until the cleanup reaches physical drain. */
        if(failure==4)recovery_owner_pending=false;
        future_slot_t slots[2]={{.allocated=true,.driver_active=true,.state=FUTURE_STATE_PENDING,
            .driver=&recovery_driver,.driver_state=&public_state}};
        future_runtime_t future={.slots=slots};
        esp32_mquickjs_runtime_t runtime={.future_state=&future};JSContext ctx=0;s_active_runtime=&runtime;
        fail_at=failure;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!vm_frees && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity);
        assert(s_wifi_state.cleanup_error==-failure && !temporary);
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!vm_frees && s_active_runtime==&runtime && !phase_calls[2]);
        unsigned stopped=stop_calls,ap_retired=ap_calls,sta_retired=sta_calls;
        fail_at=0;hold_worker=false;
        bool destroyed=esp32_mquickjs_destroy_internal(&ctx,&runtime,false);
        if(!destroyed)destroyed=esp32_mquickjs_destroy_internal(&ctx,&runtime,false);
        assert(destroyed && vm_frees==1 && !s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
        if(failure>1)assert(stop_calls==stopped);
        if(failure>2)assert(ap_calls==ap_retired);
        if(failure>3)assert(sta_calls==sta_retired);
    }
    /* No admitted recovery is not permission to initiate STOP or recovery. */
    setup_restart();recovery_admitted=false;recovery_owner_pending=false;
    assert(esp32_mquickjs_prepare_wifi_recovery_runtime_destroy());
    assert(!stop_calls && !owner_releases && running);
    assert(!wifi_begin_configuration_cleanup(false));
    assert(esp32_mquickjs_prepare_wifi_recovery_runtime_destroy());
    assert(!stop_calls && running && s_wifi_configuration_cleanup);
    return 0;
}
'''
