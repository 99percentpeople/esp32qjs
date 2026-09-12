"""Failure injection against the production Wi-Fi helper cleanup function."""
import pathlib
import unittest
from test_wireless_control_regression import function, compile_run

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'

class WiFiCleanupRegression(unittest.TestCase):
    def code(self):
        body = ''.join(function(SOURCE.read_text(), name) for name in ['wifi_reset_helper_state', 'wifi_retire_station_netif', 'wifi_cleanup_helper', 'wifi_cleanup_failed_init'])
        return r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT -33
#define portMAX_DELAY UINT32_MAX
static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { return fallback; }
#define ESP_ERR_INVALID_STATE -2
#define TAG "test"
#define ESP_LOGE(...) ((void)0)
#define IP_EVENT 1
#define WIFI_EVENT 2
#define ESP32QJS_WIFI_CONTROL_EVENT 3
#define WIFI_CONTROL_LINK_DRAINED 3
#define IP_EVENT_STA_GOT_IP 4
#define WIFI_EVENT_SCAN_DONE 5
#define WIFI_EVENT_STA_DISCONNECTED 6
#define WIFI_EVENT_STA_START 7
#define WIFI_EVENT_STA_CONNECTED 12
#define pdMS_TO_TICKS(ms) (ms)
typedef int esp_err_t;
typedef struct { void *lock,*event_group,*scan_queue,*connect_queue,*driver_event_queue; } esp32_mquickjs_wifi_runtime_resources_t;
static struct {
    void *connect_timeout_timer,*ip_event_instance,*wifi_scan_event_instance,*control_event_instance;
    void *wifi_disconnect_event_instance,*wifi_connected_event_instance,*wifi_start_event_instance,*sta_netif;
    void *lock,*event_group,*scan_queue,*connect_queue,*driver_event_queue;
    atomic_uint callbacks_active;
    int radio_lease,cleanup_error,sta_detach_error;
    const char *cleanup_stage;
    bool cleanup_timer_stopped,cleanup_radio_released,initialized,runtime_cleanup_pending;
    uint32_t scan_generation,connect_generation,disconnect_epoch;bool disconnect_epoch_exhausted;
} s_wifi_state;
static struct { unsigned identity; } s_wifi_lifecycle;
static int boundary(int n);
static int esp32_mquickjs_wifi_radio_finish_lifecycle(void *t,bool shutdown) { (void)t;(void)shutdown;int err=boundary(10);if(!err)s_wifi_lifecycle.identity=0;return err; }
static int esp32_mquickjs_wifi_radio_quiesce_lifecycle(void *t) { (void)t;return boundary(9); }
static int calls[13], fail_at, freed, released;
static atomic_uint s_wifi_watch_control_mask;
static int s_wifi_runtime_resource_ops;
static int boundary(int n) { calls[n]++;return n==fail_at ? -n : ESP_OK; }
static void wifi_stop_connect_timeout_timer(void) { (void)boundary(1); }
static int esp_timer_stop_blocking(void *t,unsigned ticks) { assert(t && ticks);return boundary(1); }
static int esp_timer_delete(void *t) { assert(t);return boundary(2); }
static int esp_event_handler_instance_unregister(int base,int id,void *h) { (void)base;assert(h);return boundary(id); }
static void vTaskDelay(int n) { (void)n;assert(0); }
static int esp32_mquickjs_wifi_radio_release_and_stop_idle(int *lease) { int err=boundary(8);if(err)return err;*lease=0;released++;return 0; }
static int esp_wifi_clear_default_wifi_driver_and_handlers(void *n) { assert(n);return boundary(11); }
static void esp_netif_destroy(void *n) { assert(n);freed++; }
/* SDK-facing retirement boundary; its scheduler is covered separately by
 * test_wifi_netif_retirement using the complete production translation unit. */
static int esp32_mquickjs_wifi_netif_retire(void **n,int *error) {
    if(*error)return *error;
    if(!*n)return ESP_OK;
    *error=esp_wifi_clear_default_wifi_driver_and_handlers(*n);
    if(!*error){esp_netif_destroy(*n);*n=NULL;}
    return *error;
}
static void esp32_mquickjs_wifi_runtime_resources_deinit(esp32_mquickjs_wifi_runtime_resources_t *r,int *ops) {
    (void)ops;assert(r->lock && r->event_group && r->scan_queue && r->connect_queue && r->driver_event_queue);freed++;
}
''' + body

    def test_failed_cleanup_retains_callback_storage_and_retries_only_suffix(self):
        compile_run(self, self.code() + r'''
int main(void) {
    for(int stage=1;stage<=8;stage++) {
        memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));freed=released=0;
        s_wifi_state.connect_timeout_timer=s_wifi_state.ip_event_instance=s_wifi_state.wifi_scan_event_instance=(void *)1;
        s_wifi_state.wifi_disconnect_event_instance=s_wifi_state.wifi_start_event_instance=s_wifi_state.sta_netif=(void *)1;
        s_wifi_state.lock=s_wifi_state.event_group=s_wifi_state.scan_queue=s_wifi_state.connect_queue=s_wifi_state.driver_event_queue=(void *)1;
        s_wifi_state.radio_lease=1;s_wifi_state.control_event_instance=(void *)1;
        s_wifi_state.scan_generation=s_wifi_state.connect_generation=s_wifi_state.disconnect_epoch=UINT32_MAX;
        s_wifi_state.disconnect_epoch_exhausted=true;
        fail_at=stage;wifi_cleanup_failed_init();
        assert(freed==0 && released==0 && s_wifi_state.lock && s_wifi_state.radio_lease);
        assert(s_wifi_state.cleanup_error==-stage && s_wifi_state.cleanup_stage);
        for(int i=1;i<=8;i++) assert(calls[i]==(i<=stage));
        fail_at=0;wifi_cleanup_failed_init();
        assert(freed==2 && released==1 && !s_wifi_state.lock && !s_wifi_state.cleanup_error);
        assert(s_wifi_state.scan_generation==UINT32_MAX && s_wifi_state.connect_generation==UINT32_MAX);
        assert(s_wifi_state.disconnect_epoch==UINT32_MAX && s_wifi_state.disconnect_epoch_exhausted);
        for(int i=1;i<=8;i++) assert(calls[i]==1+(i==stage));
        wifi_cleanup_failed_init();
        assert(freed==2 && released==1);
    }
    /* A failed quiesce retains netif/queues and excludes new admission. */
    s_wifi_state.lock=s_wifi_state.event_group=s_wifi_state.scan_queue=s_wifi_state.connect_queue=s_wifi_state.driver_event_queue=(void *)1;
    s_wifi_state.sta_netif=(void *)1;s_wifi_state.radio_lease=1;
    s_wifi_state.runtime_cleanup_pending=true;s_wifi_lifecycle.identity=1;
    memset(calls,0,sizeof(calls));freed=released=0;fail_at=9;
    assert(wifi_cleanup_failed_init()==-9);
    assert(!freed && released==1 && s_wifi_lifecycle.identity && !s_wifi_state.radio_lease);
    fail_at=0;assert(wifi_cleanup_failed_init()==0);
    assert(freed==2 && released==1 && calls[8]==1 && calls[9]==2 && calls[10]==1);
    assert(!s_wifi_lifecycle.identity && !s_wifi_state.runtime_cleanup_pending);
    /* stop before first init, plus final reservation release failure/retry. */
    s_wifi_state.runtime_cleanup_pending=true;s_wifi_lifecycle.identity=2;fail_at=10;
    assert(wifi_cleanup_failed_init()==-10 && s_wifi_lifecycle.identity==2);
    fail_at=0;assert(wifi_cleanup_failed_init()==0);
    assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_lifecycle.identity);
    /* Failed detach has already destroyed the driver; never retry it. */
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));freed=released=0;
    s_wifi_state.lock=s_wifi_state.event_group=s_wifi_state.scan_queue=s_wifi_state.connect_queue=s_wifi_state.driver_event_queue=(void *)1;
    s_wifi_state.sta_netif=(void *)1;s_wifi_state.radio_lease=1;s_wifi_lifecycle.identity=3;fail_at=11;
    assert(wifi_cleanup_failed_init()==-11 && s_wifi_state.sta_detach_error==-11);
    assert(!freed && calls[11]==1 && s_wifi_lifecycle.identity==3);
    fail_at=0;assert(wifi_cleanup_failed_init()==-11 && calls[11]==1 && !freed);
    /* Separate fixture: helper-only retirement must not release caller token. */
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));s_wifi_lifecycle.identity=4;
    assert(wifi_cleanup_helper(false)==0 && s_wifi_lifecycle.identity==4 && !calls[10]);
}
''')

    def test_callback_timeout_keeps_storage_and_does_not_repeat_unregistration(self):
        code = self.code().replace(
            'static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { return fallback; }',
            'static unsigned remaining=2; static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { (void)fallback;return remaining; }')
        code = code.replace('static void vTaskDelay(int n) { (void)n;assert(0); }',
                            'static void vTaskDelay(int n) { assert(n==1 && remaining);remaining--; }')
        compile_run(self, code + r'''
int main(void) {
    s_wifi_state.lock=s_wifi_state.event_group=s_wifi_state.scan_queue=s_wifi_state.connect_queue=s_wifi_state.driver_event_queue=(void *)1;
    s_wifi_state.connect_timeout_timer=s_wifi_state.wifi_start_event_instance=s_wifi_state.sta_netif=(void *)1;
    s_wifi_state.radio_lease=1;s_wifi_lifecycle.identity=3;s_wifi_state.runtime_cleanup_pending=true;
    atomic_store(&s_wifi_state.callbacks_active,1);
    assert(wifi_cleanup_failed_init()==ESP_ERR_TIMEOUT && !remaining);
    assert(!strcmp(s_wifi_state.cleanup_stage,"callbacks-drain") && s_wifi_state.lock && s_wifi_state.radio_lease);
    assert(s_wifi_lifecycle.identity==3 && !freed && !released && !calls[8]);
    assert(calls[1]==1 && calls[2]==1 && calls[7]==1 && !s_wifi_state.wifi_start_event_instance);
    remaining=10;atomic_store(&s_wifi_state.callbacks_active,0);
    assert(wifi_cleanup_failed_init()==ESP_OK && !s_wifi_lifecycle.identity);
    assert(freed==2 && released==1 && calls[1]==1 && calls[2]==1 && calls[7]==1);
}
''')

    def test_runtime_attach_does_not_reuse_pending_native_resources(self):
        source = SOURCE.read_text()
        start = source.index('bool esp32_mquickjs_init_wifi_runtime(')
        body = source[start:source.index('\n}\n', start) + 3]
        compile_run(self, r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
typedef int JSContext,esp32_mquickjs_runtime_t;
static struct { const char *cleanup_stage;int cleanup_error;bool runtime_cleanup_pending,connect_in_progress,connect_draining;struct { bool connected; } status;unsigned scan_generation,connect_generation; } s_wifi_state;
static esp32_mquickjs_runtime_t *s_wifi_runtime;
static void wifi_lock(void) {}
static void wifi_unlock(void) {}
static bool esp32_mquickjs_init_wifi_ap_runtime(JSContext *ctx) { (void)ctx;return true; }
static bool esp32_mquickjs_init_wifi_wake_runtime(JSContext *ctx) { (void)ctx;return true; }
static bool cleanup_fails;
static int cleanup_calls,attached,pollers,errors,connect_cancels,scan_cancels;
static int esp32_mquickjs_wifi_cancel_connect(unsigned generation) { (void)generation;connect_cancels++;return 0; }
static int esp32_mquickjs_wifi_cancel_scan(unsigned generation) { (void)generation;scan_cancels++;return 0; }
static int wifi_finish_runtime_cleanup(bool wait) {
    (void)wait;
    cleanup_calls++;
    if(cleanup_fails) return s_wifi_state.cleanup_error;
    s_wifi_state.runtime_cleanup_pending=false;s_wifi_state.cleanup_stage=NULL;s_wifi_state.cleanup_error=0;return 0;
}
static const char *esp_err_to_name(int err) { (void)err;return "fixture"; }
static int JS_ThrowInternalError(JSContext *c,const char *fmt,...) { (void)c;(void)fmt;errors++;return 0; }
static bool esp32_mquickjs_init_wifi_future_runtime(JSContext *c,esp32_mquickjs_runtime_t *r) { (void)c;(void)r;attached++;return true; }
static bool esp32_mquickjs_init_wifi_raw_tx_runtime(JSContext *c,esp32_mquickjs_runtime_t *r) { (void)c;(void)r;return true; }
static bool esp32_mquickjs_init_wifi_action_runtime(JSContext *c,esp32_mquickjs_runtime_t *r) { (void)c;(void)r;return true; }
static int wifi_driver_event_poller;
static bool esp32_mquickjs_register_async_poller(esp32_mquickjs_runtime_t *r,int p,void *o) { (void)r;(void)p;(void)o;pollers++;return true; }
''' + body + r'''
int main(void) {
    JSContext ctx=0;esp32_mquickjs_runtime_t runtime=0;
    s_wifi_state.cleanup_stage="scan-unregister";s_wifi_state.cleanup_error=-4;cleanup_fails=true;
    assert(!esp32_mquickjs_init_wifi_runtime(&ctx,&runtime));
    assert(cleanup_calls==1 && errors==1 && !s_wifi_runtime && !attached && !pollers);
    s_wifi_state.runtime_cleanup_pending=true;s_wifi_state.connect_draining=true;
    assert(!esp32_mquickjs_init_wifi_runtime(&ctx,&runtime));
    assert(connect_cancels==1 && scan_cancels==1 && !attached && !pollers);
    cleanup_fails=false;
    assert(esp32_mquickjs_init_wifi_runtime(&ctx,&runtime));
    assert(cleanup_calls==3 && s_wifi_runtime==&runtime && attached==1 && pollers==1);
    assert(connect_cancels==2 && scan_cancels==2 && !s_wifi_state.runtime_cleanup_pending);
}
''')
