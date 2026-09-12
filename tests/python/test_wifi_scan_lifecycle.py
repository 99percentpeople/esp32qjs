"""Production scan cancellation and terminal callbacks with controlled SDK timing."""
import pathlib
import unittest
from test_wireless_control_regression import compile_run

ROOT = pathlib.Path(__file__).resolve().parents[2]
WIFI = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'
FUTURE = WIFI.with_name('esp32_mquickjs_wifi_future.c')

def extract(source, name):
    import re
    match = re.search(r'(?:static )?[\w *]+\b' + name + r'\([^;{}]*\)\n\{', source)
    assert match, name
    return source[match.start():source.index('\n}\n', match.start()) + 3]

SDK = r'''
/* Vendor IE slots are absent in these connection teardown cases. */
static void esp32_mquickjs_deinit_wifi_vendor_ie_watch_runtime(void) {}
static int esp32_mquickjs_wifi_radio_vendor_ie_clear(int interface) {(void)interface;return 0;}

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define ESP32_MQUICKJS_CANCEL_REJECTED 0
#define ESP32_MQUICKJS_CANCELLED 1
#define WIFI_FUTURE_SCAN 0
#define WIFI_FUTURE_CONNECT 1
#define WIFI_FUTURE_DISCONNECT 2
#define WIFI_DRIVER_EVENT_STARTED 1
#define WIFI_DRIVER_EVENT_DISCONNECTED 2
#define WIFI_DRIVER_EVENT_SCAN_DONE 3
#define WIFI_DRIVER_EVENT_GOT_IP 4
#define WIFI_DRIVER_EVENT_CONNECT_TIMEOUT 5
#define WIFI_DRIVER_EVENT_LINK_DRAINED 6
#define WIFI_LINK_DRAINED_BIT 8
#define ESP32QJS_WIFI_CONTROL_EVENT 1
#define WIFI_CONTROL_LINK_DRAINED 1
#define ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT 2
#define ESP32_MQUICKJS_WIFI_OPERATION_CONNECT 1
#define ESP32_MQUICKJS_WIFI_OPERATION_NONE 0
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED 4
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE 2
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS 1
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT 3
#define WIFI_STARTED_BIT 4
#define WIFI_CONNECTED_BIT 1
#define WIFI_FAILED_BIT 2
#define pdTRUE 1
typedef struct { unsigned generation,identity;int client;bool acquired; } esp32_mquickjs_wifi_radio_lease_t;
typedef enum { ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NONE, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT } esp32_mquickjs_wifi_radio_operation_kind_t;
typedef struct { unsigned generation,lease_identity,identity;esp32_mquickjs_wifi_radio_operation_kind_t kind; } esp32_mquickjs_wifi_radio_operation_t;
typedef int JSContext,esp32_mquickjs_runtime_t,esp_err_t,esp32_mquickjs_cancel_result_t,esp32_mquickjs_future_token_t;
typedef struct { uint8_t *ssid,*bssid;int channel; } wifi_scan_config_t;
typedef struct { struct { unsigned char ssid[32],password[64]; struct { bool capable,required; } pmf_cfg; } sta; } wifi_config_t;
typedef struct { int kind,reason;unsigned status,generation; } esp32_mquickjs_wifi_driver_event_t;
typedef struct { bool valid;uint8_t ssid[32],ssid_len,bssid[6],channel;uint16_t aid; } esp32_mquickjs_wifi_link_snapshot_t;
typedef struct { unsigned generation,status; } esp32_mquickjs_wifi_scan_event_t;
typedef struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    esp32_mquickjs_wifi_radio_operation_t radio_operation;
    bool scan_start_active,connect_start_active;
    bool runtime_cleanup_pending,cleanup_timer_stopped;int cleanup_error;atomic_uint callbacks_active;
    const char *cleanup_stage;
    bool scan_in_progress,scan_future_registered,scan_draining,scan_results_pending;
    bool scan_stop_submitted,scan_stop_active;
    int scan_cleanup_error;
    wifi_scan_config_t native_scan_config;
    uint8_t native_scan_ssid[33], native_scan_bssid[6];
    bool initialized,started,connect_in_progress,connect_future_registered;
    bool connect_draining,disconnect_active,disconnect_submitted,disconnect_seen;
    bool disconnect_fence_posted,disconnect_fence_seen,disconnect_epoch_exhausted;
    unsigned disconnect_epoch;int disconnect_cleanup_error;void *sta_netif;
    unsigned scan_generation,connect_generation,connection_future_operation;
    esp32_mquickjs_future_token_t scan_future_token,connect_future_token;
    void *scan_queue,*connect_queue,*event_group;
    struct { bool scanning,started,connected;int last_disconnect_reason;esp32_mquickjs_wifi_link_snapshot_t link; } status;
} esp32_mquickjs_wifi_state_t;
typedef struct {
    int kind;bool started,completed,cancel_requested;unsigned generation;
    void *runtime;int token;unsigned timeout_ms,connect_kind;int64_t connect_start_us;
    wifi_scan_config_t scan_config;wifi_config_t connect_config;
} esp32_mquickjs_future_driver_state_t;
static esp32_mquickjs_wifi_state_t s_wifi_state={.sta_netif=(void *)1};
static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime=(void *)1;
static esp32_mquickjs_wifi_radio_lease_t s_wifi_application;
static void esp32_mquickjs_deinit_wifi_ap_runtime(void) {}
/* These single-Station cases never enter AP/STA coordinated cleanup; the full
 * production coordinator is exercised by test_wifi_configuration_cleanup. */
static bool s_wifi_configuration_cleanup,s_wifi_ap_stop_cleanup;
static int wifi_adopt_ap_stop_cleanup(void) { assert(0);return ESP_ERR_INVALID_STATE; }
static const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void) { return NULL; }
static int wifi_begin_configuration_cleanup(bool allow_disconnect) { (void)allow_disconnect;assert(0);return ESP_ERR_INVALID_STATE; }
static int wifi_finish_configuration_cleanup(void) { assert(0);return ESP_ERR_INVALID_STATE; }

static void esp32_mquickjs_deinit_wifi_wake_runtime(void) {}
static void esp32_mquickjs_deinit_wifi_watch_runtime(void) {}
static void esp32_mquickjs_wifi_radio_release(esp32_mquickjs_wifi_radio_lease_t *l) { memset(l,0,sizeof(*l)); }
static int admission_error,admissions,operation_releases;
static int lock_depth,stops,clears,wakes,queued,stop_error,clear_error,starts;
static int last_generation,start_error;
static bool complete_in_stop;
esp_err_t esp32_mquickjs_wifi_drain_scan(void);
static void wifi_process_driver_event(const esp32_mquickjs_wifi_driver_event_t *e);
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
static unsigned ticks;
static void (*on_delay)(void);
static unsigned xTaskGetTickCount(void) { return ticks; }
static void vTaskDelay(unsigned n) { ticks+=n;if(on_delay)on_delay(); }
static esp32_mquickjs_wifi_state_t *esp32_mquickjs_wifi_state(void) { return &s_wifi_state; }
static void wifi_lock(void) { assert(!lock_depth++); }
static void wifi_unlock(void) { assert(--lock_depth==0); }
static void esp32_mquickjs_wifi_lock(void) { wifi_lock(); }
static void esp32_mquickjs_wifi_unlock(void) { wifi_unlock(); }
static int esp32_mquickjs_wifi_radio_begin_operation(esp32_mquickjs_wifi_radio_lease_t *lease,esp32_mquickjs_wifi_radio_operation_kind_t kind,esp32_mquickjs_wifi_radio_operation_t *op) {
    (void)lease;assert(!lock_depth);admissions++;
    if(admission_error)return admission_error;
    *op=(esp32_mquickjs_wifi_radio_operation_t){.identity=(unsigned)admissions,.kind=kind};return 0;
}
static void esp32_mquickjs_wifi_radio_end_operation(esp32_mquickjs_wifi_radio_operation_t *op) { assert(!lock_depth);if(op->identity)operation_releases++;memset(op,0,sizeof(*op)); }
static int esp32_mquickjs_wifi_radio_validate_scan_channels(const esp32_mquickjs_wifi_radio_operation_t *op,const wifi_scan_config_t *config) { assert(!lock_depth && op->identity);(void)config;return 0; }
static void wifi_set_scanning_locked(bool v) { s_wifi_state.scan_in_progress=s_wifi_state.status.scanning=v; }
static void esp32_mquickjs_wifi_set_scanning_locked(bool v) { wifi_set_scanning_locked(v); }
static void esp32_mquickjs_wifi_clear_scan_future(void) { s_wifi_state.scan_future_registered=false;s_wifi_state.scan_future_token=0; }
static void esp32_mquickjs_wifi_clear_connect_future(void) { s_wifi_state.connect_future_registered=false; }
static void wifi_stop_connect_timeout_timer(void) {}
static int cleanup_calls,cleanup_failure;
static int wifi_cleanup_failed_init(void) { cleanup_calls++;if(cleanup_failure)return cleanup_failure;s_wifi_state.runtime_cleanup_pending=false;s_wifi_state.cleanup_stage=NULL;s_wifi_state.cleanup_error=0;s_wifi_state.initialized=false;return 0; }
static int esp32_mquickjs_wifi_prepare_connect_timer(void) { return 0; }
static void wifi_queue_connect_event(unsigned g,int k,int r,const esp32_mquickjs_wifi_link_snapshot_t *link) { (void)g;(void)k;(void)r;(void)link; }
static unsigned event_bits;
static void xEventGroupClearBits(void *g,unsigned b) { (void)g;event_bits &= ~b; }
static void xEventGroupSetBits(void *g,unsigned b) { (void)g;event_bits |= b; }
static void xQueueOverwrite(void *q,const esp32_mquickjs_wifi_scan_event_t *e) { (void)q;queued++;last_generation=e->generation; }
static int esp32_mquickjs_future_wake(void *r,int t) { (void)r;(void)t;wakes++;return 0; }
static void esp32_mquickjs_notify_activity(void *r) { (void)r; }
static void xQueueReset(void *q) { (void)q; }
static int esp32_mquickjs_wifi_ensure_started(void) { s_wifi_state.initialized=true;s_wifi_state.started=true;return 0; }
static int64_t esp_timer_get_time(void) { return 1000; }
static int prepare_connect_error,prepare_connect_calls;
static int esp32_mquickjs_wifi_prepare_connect(wifi_config_t *config) { assert(config);prepare_connect_calls++;return prepare_connect_error ? prepare_connect_error : esp32_mquickjs_wifi_ensure_started(); }
static void JS_ThrowInternalError(JSContext *c,const char *m) { (void)c;(void)m; }
static void esp32_mquickjs_wifi_throw_scan_error(JSContext *c,int e) { (void)c;(void)e; }
static void esp32_mquickjs_wifi_throw_connect_error(JSContext *c,int e) { (void)c;(void)e; }
static void esp32_mquickjs_wifi_throw_operation_error(JSContext *c,const char *code,const char *op,int e,int reason,unsigned status) { (void)c;(void)code;(void)op;(void)e;(void)reason;(void)status; }
static const char *wifi_future_operation_name(int kind) { (void)kind;return "wifi.scan"; }
static int esp32_mquickjs_wifi_start_connect(const void *config,unsigned timeout) { (void)config;(void)timeout;return 0; }
static int esp32_mquickjs_wifi_start_disconnect(bool *pending) { *pending=false;return 0; }
static void esp32_mquickjs_wireless_secure_zero(void *v,size_t n) { memset(v,0,n); }
static void heap_caps_free(void *p) { free(p); }
static int disconnect_calls,disconnect_error,posts,post_error;static unsigned fence_epoch;
static bool netif_up,complete_disconnect_in_call;
static bool esp_netif_is_netif_up(void *netif) { assert(netif && !lock_depth);return netif_up; }
static int esp_event_post(int base,int id,const void *data,size_t size,unsigned wait) {
    (void)base;(void)id;assert(size==sizeof(unsigned) && !wait && !lock_depth);
    posts++;if(!post_error)fence_epoch=*(const unsigned *)data;return post_error;
}
static int esp_wifi_disconnect(void) {
    assert(!lock_depth);disconnect_calls++;
    if(complete_disconnect_in_call) {
        const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
        wifi_process_driver_event(&done);
    }
    return disconnect_error;
}
static int esp_wifi_scan_stop(void) {
    assert(!lock_depth);stops++;
    if(complete_in_stop) { const esp32_mquickjs_wifi_driver_event_t e={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};wifi_process_driver_event(&e);(void)esp32_mquickjs_wifi_drain_scan();assert(clears==0); }
    return stop_error;
}
static int esp_wifi_clear_ap_list(void) { assert(!lock_depth);clears++;return clear_error; }
static int esp_wifi_scan_start(const wifi_scan_config_t *c,bool block) { assert(!lock_depth && !block);assert(c==&s_wifi_state.native_scan_config);starts++;return start_error; }
'''

SDK += extract(WIFI.read_text(), 'esp32_mquickjs_wifi_connection_reserved_locked')


class WiFiScanLifecycle(unittest.TestCase):
    def code(self):
        source = WIFI.read_text()
        helpers = ''
        for name in ['wifi_release_radio_operation','wifi_prepare_radio_operation','esp32_mquickjs_wifi_drain_scan', 'wifi_start_scan_reserved','esp32_mquickjs_wifi_start_scan', 'esp32_mquickjs_wifi_cancel_scan', 'wifi_begin_disconnect_locked', 'wifi_finish_disconnect_locked', 'wifi_post_disconnect_fence', 'wifi_request_disconnect', 'esp32_mquickjs_wifi_cancel_connect']:
            if name + '(' in source:
                helpers += extract(source, name)
        return SDK + helpers + extract(source, 'wifi_process_driver_event') + extract(source, 'wifi_finish_runtime_cleanup') + extract(source, 'esp32_mquickjs_deinit_wifi_runtime') + ''.join(extract(FUTURE.read_text(), name) for name in ['wifi_future_start', 'wifi_future_cancel', 'wifi_future_destroy'])

    def test_cancel_retains_native_scan_until_late_terminal(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_state.scan_generation=7;s_wifi_state.scan_in_progress=true;
    s_wifi_state.scan_future_registered=true;s_wifi_state.scan_queue=(void *)1;
    esp32_mquickjs_future_driver_state_t future={.kind=WIFI_FUTURE_SCAN,.started=true,.generation=7};
    assert(wifi_future_cancel(&future)==ESP32_MQUICKJS_CANCELLED);
    assert(future.completed && !s_wifi_state.scan_future_registered);
    assert(s_wifi_state.scan_in_progress); /* Public cancellation is not native completion. */
    assert(stops==1 && clears==0);
    const esp32_mquickjs_wifi_driver_event_t event={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&event);
    assert(!s_wifi_state.scan_in_progress && queued==0);
}
''')

    def test_future_admission_waits_for_terminal_and_result_cleanup(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_state.scan_queue=(void *)1;
    esp32_mquickjs_future_driver_state_t a={.kind=WIFI_FUTURE_SCAN},b={.kind=WIFI_FUTURE_SCAN},connect={.kind=WIFI_FUTURE_CONNECT};
    assert(wifi_future_start(NULL,NULL,1,&a));
    assert(wifi_future_cancel(&a)==ESP32_MQUICKJS_CANCELLED);
    assert(!wifi_future_start(NULL,NULL,2,&b));
    assert(!wifi_future_start(NULL,NULL,3,&connect));
    assert(starts==1 && stops==1 && clears==0);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);
    clear_error=-9;
    assert(!wifi_future_start(NULL,NULL,2,&b));
    assert(s_wifi_state.scan_draining && s_wifi_state.scan_cleanup_error==-9 && starts==1);
    clear_error=0;
    assert(wifi_future_start(NULL,NULL,2,&b));
    assert(b.generation!=a.generation && starts==2 && !queued);
    assert(esp32_mquickjs_wifi_cancel_scan(a.generation)==0);
    assert(s_wifi_state.scan_future_registered && s_wifi_state.scan_in_progress);
    wifi_process_driver_event(&done);
    assert(queued==1 && last_generation==(int)b.generation);
}
''')

    def test_stop_failure_and_completion_inside_stop_do_not_reuse_storage(self):
        compile_run(self, self.code() + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t a={.kind=WIFI_FUTURE_SCAN},b={.kind=WIFI_FUTURE_SCAN};
    assert(wifi_future_start(NULL,NULL,1,&a));
    stop_error=-8;
    assert(wifi_future_cancel(&a)==ESP32_MQUICKJS_CANCELLED);
    assert(s_wifi_state.scan_in_progress && s_wifi_state.scan_draining && s_wifi_state.scan_cleanup_error==-8);
    assert(!wifi_future_start(NULL,NULL,2,&b));
    assert(stops==1); /* Admission must not resubmit a failed stop. */
    stop_error=0;complete_in_stop=true;
    assert(esp32_mquickjs_wifi_cancel_scan(a.generation)==0); /* explicit cleanup retry */
    assert(stops==2 && clears==1 && !s_wifi_state.scan_draining);
    assert(wifi_future_start(NULL,NULL,2,&b));
}
''')

    def test_destroy_completed_unread_scan_frees_results_and_start_failure_rolls_back(self):
        compile_run(self, self.code() + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t *a=calloc(1,sizeof(*a));
    a->kind=WIFI_FUTURE_SCAN;
    start_error=-7;
    assert(!wifi_future_start(NULL,NULL,1,a));
    assert(!s_wifi_state.scan_in_progress && !s_wifi_state.scan_future_registered);
    start_error=0;
    assert(wifi_future_start(NULL,NULL,2,a));
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);a->completed=true;
    wifi_future_destroy(a);
    assert(clears==1 && stops==0 && !s_wifi_state.scan_results_pending && !s_wifi_state.scan_future_registered);
}
''')

    def test_generation_exhaustion_and_prepared_cancel_have_no_native_effect(self):
        compile_run(self, self.code() + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t a={.kind=WIFI_FUTURE_SCAN};
    s_wifi_state.scan_generation=UINT32_MAX;
    assert(!wifi_future_start(NULL,NULL,1,&a));
    assert(wifi_future_cancel(&a)==ESP32_MQUICKJS_CANCELLED);
    assert(!stops && !starts && !clears && s_wifi_state.scan_generation==UINT32_MAX);
    a=(esp32_mquickjs_future_driver_state_t){.kind=WIFI_FUTURE_CONNECT};
    s_wifi_state.connect_generation=UINT32_MAX;
    assert(!wifi_future_start(NULL,NULL,2,&a));
    assert(!s_wifi_state.connect_future_registered);
}
''')

    def test_runtime_teardown_keeps_cancelled_scan_quarantined_across_attach(self):
        compile_run(self, self.code() + r'''
int main(void) {
    s_wifi_state.scan_queue=(void *)1;
    esp32_mquickjs_future_driver_state_t *a=calloc(1,sizeof(*a)),b={.kind=WIFI_FUTURE_SCAN};
    a->kind=WIFI_FUTURE_SCAN;a->scan_config.channel=6;
    assert(wifi_future_start(NULL,NULL,1,a));
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    wifi_future_destroy(a);
    assert(!s_wifi_runtime && !s_wifi_state.scan_future_registered);
    assert(s_wifi_state.scan_in_progress && s_wifi_state.scan_draining && stops==1);
    assert(s_wifi_state.native_scan_config.channel==6);
    assert(wifi_finish_runtime_cleanup(false)==ESP_ERR_INVALID_STATE);
    assert(!wifi_future_start(NULL,NULL,2,&b));
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);
    assert(!queued);
    assert(wifi_finish_runtime_cleanup(true)==ESP_OK);
    s_wifi_runtime=(void *)2;
    assert(wifi_future_start(NULL,NULL,2,&b));
    assert(starts==2 && clears==1);
}
''')
