"""Production Wi-Fi runtime retirement, including delayed native terminals."""
import unittest
import test_wifi_scan_lifecycle as fixture
from test_wireless_control_regression import compile_run
from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT

class WiFiRuntimeCleanup(unittest.TestCase):
    def test_established_connection_is_disconnected_on_runtime_teardown(self):
        compile_run(self,fixture.WiFiScanLifecycle().code()+r'''
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.started=true;
    s_wifi_state.status.connected=true;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(disconnect_calls==1 && s_wifi_state.connect_draining && !s_wifi_runtime);
}
''')

    def test_disconnect_terminal_and_ip_fence_both_precede_resource_release(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+r'''
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.status.connected=true;
    s_wifi_state.radio_operation.identity=9;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(!cleanup_calls && !operation_releases && disconnect_calls==1);
    const esp32_mquickjs_wifi_driver_event_t disconnected={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&disconnected);
    assert(wifi_finish_runtime_cleanup(false)==ESP_ERR_INVALID_STATE);
    assert(!cleanup_calls && !operation_releases && posts==1);
    const esp32_mquickjs_wifi_driver_event_t drained={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&drained);
    assert(wifi_finish_runtime_cleanup(false)==ESP_OK);
    assert(cleanup_calls==1 && operation_releases==1 && disconnect_calls==1);
    assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_runtime);
}
''')

    def test_attach_waits_for_native_events_without_repeating_disconnect(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+r'''
static void deliver(void) {
    assert(!s_wifi_runtime && !cleanup_calls);
    if(ticks==2) {
        const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
        wifi_process_driver_event(&done);
    }
    if(ticks==4) {
        const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
        wifi_process_driver_event(&fence);
    }
}
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.status.connected=true;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    on_delay=deliver;
    assert(wifi_finish_runtime_cleanup(true)==ESP_OK);
    assert(ticks==4 && disconnect_calls==1 && cleanup_calls==1);
}
''')

    def test_missing_terminal_bounds_wait_and_preserves_native_storage(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+r'''
int main(void) {
    s_wifi_state.initialized=true;s_wifi_state.status.connected=true;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(wifi_finish_runtime_cleanup(true)==ESP_ERR_INVALID_STATE);
    assert(ticks==1000 && disconnect_calls==1 && !cleanup_calls);
    assert(s_wifi_state.initialized && s_wifi_state.runtime_cleanup_pending);
    assert(!strcmp(s_wifi_state.cleanup_stage,"connection-drain"));
}
''')

    def test_scan_terminal_and_ap_list_cleanup_precede_resource_release(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t a={.kind=WIFI_FUTURE_SCAN};
    assert(wifi_future_start(NULL,NULL,1,&a));
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(stops==1 && !cleanup_calls && !clears && !operation_releases);
    const esp32_mquickjs_wifi_driver_event_t done={.kind=WIFI_DRIVER_EVENT_SCAN_DONE};
    wifi_process_driver_event(&done);clear_error=-9;
    assert(wifi_finish_runtime_cleanup(true)==-9);
    assert(!ticks && !cleanup_calls && clears==1 && !operation_releases);
    assert(!strcmp(s_wifi_state.cleanup_stage,"scan-drain"));
    clear_error=0;
    assert(wifi_finish_runtime_cleanup(true)==ESP_OK);
    assert(stops==1 && clears==2 && cleanup_calls==1 && operation_releases==1);
}
''')

    def test_idle_teardown_detaches_runtime_before_entered_callback_exits(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+r'''
static void callback_exit(void) {
    assert(!s_wifi_runtime && !cleanup_calls);
    atomic_store(&s_wifi_state.callbacks_active,0);
}
int main(void) {
    s_wifi_state.initialized=true;
    atomic_store(&s_wifi_state.callbacks_active,1);on_delay=callback_exit;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(ticks==1 && cleanup_calls==1 && !disconnect_calls && !stops);
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(cleanup_calls==1);
}
''')

    def test_detached_runtime_is_not_recovered_from_global_active_runtime(self):
        body=fixture.extract(fixture.WIFI.read_text(), 'wifi_queue_connect_event')
        compile_run(self, r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
typedef int esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;
''' + structure((COMPONENT / 'internal/esp32_mquickjs_types.h').read_text(), 'esp32_mquickjs_wifi_link_snapshot_t') + structure((COMPONENT / 'internal/esp32_mquickjs_wifi.h').read_text(), 'esp32_mquickjs_wifi_connect_event_t') + r'''
static struct { void *connect_queue;bool connect_future_registered;int connect_future_token;uint32_t connect_generation; } s_wifi_state;
static int64_t esp_timer_get_time(void) {return 12345;}
static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime;
static int writes,wakes,notifications;
static void wifi_lock(void) {}
static void wifi_unlock(void) {}
static void xQueueOverwrite(void *q,const void *e) { assert(q && e);writes++; }
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void) { return (void *)1; }
static int esp32_mquickjs_future_wake(void *r,int token) { assert(r && token);wakes++;s_wifi_runtime=NULL;return 0; }
static void esp32_mquickjs_notify_activity(void *r) { assert(r);notifications++; }
''' + body + r'''
int main(void) {
    s_wifi_state.connect_queue=(void *)1;s_wifi_state.connect_future_registered=true;s_wifi_state.connect_future_token=7;s_wifi_state.connect_generation=1;
    wifi_queue_connect_event(1,1,0,NULL);
    assert(writes==1 && !wakes && !notifications);
    s_wifi_runtime=(void *)1;
    wifi_queue_connect_event(1,1,0,NULL);
    assert(writes==2 && wakes==1 && notifications==1);
}
''')
