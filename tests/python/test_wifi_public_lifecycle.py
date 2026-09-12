"""Call the production public lifecycle adapters and idle-child admission helper."""
import unittest
import test_wifi_scan_lifecycle as fixture
from test_wireless_control_regression import compile_run

class WiFiPublicLifecycle(unittest.TestCase):
    def code(self):
        code=fixture.WiFiScanLifecycle().code()
        # Replace the base fixture's single-Station boundary definitions with
        # declarations; this fixture supplies the coordinated AP boundaries.
        for name in ['esp32_mquickjs_wifi_ap_control_lease', 'wifi_begin_configuration_cleanup',
                     'wifi_finish_configuration_cleanup']:
            import re
            code=re.sub(r'(static [^\n{};]*\b'+name+r'\([^\n]*?\)) \{[^\n]*\}',r'\1;',code,count=1)

        code=code.replace('static void esp32_mquickjs_wifi_throw_operation_error', 'static int esp32_mquickjs_wifi_throw_operation_error')
        code=code.replace('(void)reason;(void)status; }', '(void)reason;(void)status;return -99; }')
        code += r'''
typedef int JSValue;
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION 3
static struct { unsigned identity; } s_wifi_lifecycle;
static bool s_wifi_configuration_cleanup,s_wifi_configuration_stop_only;
static int s_wifi_configuration_mode;
static bool ap_control,configuration_admission_denied;
static int configuration_begin_calls;
static bool wifi_helpers_idle(bool allow_connected);
static esp32_mquickjs_wifi_radio_lease_t ap_lease;
static const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void) { return ap_control?&ap_lease:NULL; }
static int wifi_begin_configuration_cleanup(bool allow_disconnect) {
    configuration_begin_calls++;
    if(!wifi_helpers_idle(allow_disconnect) || configuration_admission_denied)return ESP_ERR_INVALID_STATE;
    assert(ap_control && !s_wifi_lifecycle.identity);
    s_wifi_configuration_cleanup=true;s_wifi_configuration_mode=0;s_wifi_configuration_stop_only=false;
    s_wifi_lifecycle.identity=7;ap_control=false;return 0;
}
static int configuration_cleanup_calls,configuration_cleanup_error;
static int wifi_finish_configuration_cleanup(void) {
    assert(s_wifi_configuration_cleanup);configuration_cleanup_calls++;
    if(configuration_cleanup_error)return configuration_cleanup_error;
    s_wifi_configuration_cleanup=false;s_wifi_lifecycle.identity=0;
    s_wifi_configuration_stop_only=false;s_wifi_configuration_mode=0;
    s_wifi_state.runtime_cleanup_pending=false;return 0;
}

static int init_calls,acquire_calls,begin_calls,begin_error,ensure_calls;
static int wifi_init_once(void) { init_calls++;if(s_wifi_state.runtime_cleanup_pending)return -2;s_wifi_state.initialized=true;return 0; }
static int esp32_mquickjs_wifi_radio_acquire(int client,int mode,esp32_mquickjs_wifi_radio_lease_t *lease) { assert(client==3 && mode==1);acquire_calls++;lease->acquired=true;return 0; }
static int esp32_mquickjs_wifi_radio_begin_lifecycle(const void *app,const void *sta,const void *ap,void *token) { assert(!ap && app==&s_wifi_application && sta==&s_wifi_state.radio_lease && token==&s_wifi_lifecycle);begin_calls++;if(begin_error)return begin_error;s_wifi_lifecycle.identity=1;return 0; }
static int JS_ThrowTypeError(void *ctx,const char *message) { (void)ctx;(void)message;return -98; }
static int wifi_make_status_object(void *ctx) { (void)ctx;return 123; }
#define JS_UNDEFINED -1
#define JS_EXCEPTION -99
static int wait_scope,wait_begins,wait_ends,wait_begin_error;
static bool stop_capture_error;
static bool esp32_mquickjs_wifi_capture_stop(void *ctx,int options,uint32_t *timeout) { (void)ctx;assert(options==JS_UNDEFINED);if(stop_capture_error)return false;*timeout=1000;return true; }
static int esp32_mquickjs_wifi_wait_begin(uint32_t timeout) { assert(timeout==1000 && !wait_scope);wait_begins++;if(wait_begin_error)return wait_begin_error;wait_scope=1;return 0; }
static void esp32_mquickjs_wifi_wait_end(void) { assert(wait_scope);wait_scope=0;wait_ends++; }
typedef struct { bool start_only;int mode; } esp32_mquickjs_wifi_radio_configuration_selection_t;
static int wifi_start_existing_running(const esp32_mquickjs_wifi_radio_configuration_selection_t *selection);
/* This fixture isolates public stop and the matching-running helper path.
 * Start capture/resolution/stopped execution use their own production fixtures. */
static bool esp32_mquickjs_wifi_capture_start(void *ctx,int options,esp32_mquickjs_wifi_radio_configuration_selection_t *s) {
    (void)ctx;assert(options==JS_UNDEFINED);*s=(esp32_mquickjs_wifi_radio_configuration_selection_t){true,WIFI_MODE_STA};return true;
}
static int wifi_configure_selected_interfaces(esp32_mquickjs_wifi_radio_configuration_selection_t *s,
    const void *sta,const void *accept_sta,const void *ap,const void *accept_ap,const void *controls,
    const void *start_controls,const void *execution) {
    assert(s->start_only && !sta && !accept_sta && !ap && !accept_ap && !controls && !start_controls && !execution);
    return wifi_start_existing_running(s);
}

'''
        # Resource cleanup is separately tested at every production SDK boundary.
        code=code.replace('static int wifi_cleanup_failed_init(void) { cleanup_calls++;', 'static int wifi_cleanup_failed_init(void) { cleanup_calls++;')
        source=fixture.WIFI.read_text()
        code+=''.join(fixture.extract(source,n) for n in ['wifi_helpers_idle','wifi_stop_idle','wifi_start_existing_running','js_wifi_start','js_wifi_stop'])
        return code

    def test_stop_routes_pending_configuration_to_central_cleanup(self):
        compile_run(self,self.code()+r'''
int main(void) {
    s_wifi_configuration_cleanup=true;s_wifi_lifecycle.identity=7;
    s_wifi_state.runtime_cleanup_pending=true;configuration_cleanup_error=-8;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==-99);
    assert(configuration_cleanup_calls==1 && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==7);
    assert(!begin_calls && !cleanup_calls && !disconnect_calls && !stops);
    configuration_cleanup_error=0;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==123);
    assert(configuration_cleanup_calls==2 && !s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
    assert(!s_wifi_state.runtime_cleanup_pending);
    assert(!begin_calls && !cleanup_calls && !disconnect_calls && !stops);
}
''')

    def test_stop_scope_ends_after_failure_and_capture_precedes_admission(self):
        compile_run(self,self.code()+r'''
int main(void) {
    stop_capture_error=true;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==JS_EXCEPTION && !wait_begins && !begin_calls);
    stop_capture_error=false;wait_begin_error=-77;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==JS_EXCEPTION && !wait_ends && !begin_calls);
    wait_begin_error=0;begin_error=-7;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==JS_EXCEPTION && wait_ends==1 && !wait_scope);
    assert(!cleanup_calls && !s_wifi_lifecycle.identity);
    begin_error=0;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==123 && wait_ends==2 && !wait_scope);
}
''')

    def test_arguments_rejected_before_init_or_driver_effects(self):
        compile_run(self,self.code()+r'''
int main(void) {
    assert(js_wifi_start(NULL,NULL,2,NULL)==-98);
    assert(js_wifi_stop(NULL,NULL,2,NULL)==-98);
    assert(!init_calls && !begin_calls && !acquire_calls && !cleanup_calls);
}
''')

    def test_start_is_idempotent_and_stop_releases_only_after_admission(self):
        compile_run(self,self.code()+r'''
int main(void) {
    assert(js_wifi_start(NULL,NULL,0,NULL)==123);
    assert(js_wifi_start(NULL,NULL,0,NULL)==123);
    assert(acquire_calls==1 && s_wifi_application.acquired);
    begin_error=-7;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==-99);
    assert(!cleanup_calls && s_wifi_application.acquired && !s_wifi_lifecycle.identity);
    begin_error=0;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==123);
    assert(cleanup_calls==1 && !s_wifi_application.acquired && !disconnect_calls && !stops);
}
''')

    def test_stop_never_cancels_connected_or_pending_children(self):
        compile_run(self,self.code()+r'''
int main(void) {
    bool *busy[]={&s_wifi_state.status.connected,&s_wifi_state.connect_in_progress,
        &s_wifi_state.connect_draining,&s_wifi_state.connect_start_active,&s_wifi_state.disconnect_active,
        &s_wifi_state.scan_in_progress,&s_wifi_state.scan_draining,&s_wifi_state.scan_results_pending,
        &s_wifi_state.scan_start_active,&s_wifi_state.scan_stop_active,
        &s_wifi_state.scan_future_registered,&s_wifi_state.connect_future_registered};
    for(unsigned i=0;i<sizeof(busy)/sizeof(*busy);i++) {
        *busy[i]=true;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==-99);
        assert(!begin_calls && !cleanup_calls && !disconnect_calls && !stops);
        *busy[i]=false;
    }
}
''')

    def test_cleanup_failure_retains_exclusion_and_retry_skips_admission(self):
        compile_run(self,self.code()+r'''
int main(void) {
    cleanup_failure=-6;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==-99);
    assert(begin_calls==1 && cleanup_calls==1 && s_wifi_lifecycle.identity);
    assert(js_wifi_start(NULL,NULL,0,NULL)==-99 && !acquire_calls);
    cleanup_failure=0;
    assert(js_wifi_stop(NULL,NULL,0,NULL)==123);
    assert(begin_calls==1 && cleanup_calls==2);
}
''')

    def test_healthy_ap_and_apsta_stop_use_central_retirement(self):
        compile_run(self,self.code()+r'''
int main(void) {
    for(int station=0;station<=1;station++) {
        ap_control=true;s_wifi_application.acquired=station;s_wifi_state.radio_lease.acquired=station;
        configuration_admission_denied=true;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==-99 && ap_control && !s_wifi_configuration_cleanup);
        assert(s_wifi_application.acquired==station && !configuration_cleanup_calls && !cleanup_calls);
        configuration_admission_denied=false;s_wifi_state.status.connected=true;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==-99 && ap_control && !s_wifi_configuration_cleanup);
        s_wifi_state.status.connected=false;configuration_cleanup_error=-8;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==-99 && !ap_control && s_wifi_configuration_cleanup);
        assert(s_wifi_configuration_stop_only && s_wifi_configuration_mode==(station?WIFI_MODE_APSTA:WIFI_MODE_AP));
        assert(s_wifi_lifecycle.identity==7 && !begin_calls && !cleanup_calls);
        int admitted=configuration_begin_calls;configuration_cleanup_error=0;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==123 && configuration_begin_calls==admitted);
        assert(!s_wifi_configuration_cleanup && !s_wifi_configuration_stop_only && !s_wifi_lifecycle.identity);
        assert(configuration_cleanup_calls==2 && !disconnect_calls && !stops);
        configuration_cleanup_calls=0;
    }
}
''')

    def test_partial_ap_stop_teardown_waits_for_station_terminal_and_fence(self):
        import re
        code=self.code()
        code=re.sub(r'(static int wifi_adopt_ap_stop_cleanup\(void\)) \{[^\n]*\}',r'\1;',code,count=1)
        code += r'''
static bool s_wifi_configuration_disconnect_pending;
static int ap_adoptions;
static int esp32_mquickjs_wifi_ap_adopt_partial_stop(const void *token) {
    assert(token==&s_wifi_lifecycle && s_wifi_lifecycle.identity==41);
    assert(!s_wifi_runtime && !s_wifi_state.status.connected && !s_wifi_state.connect_draining);
    assert(!s_wifi_state.disconnect_active && s_wifi_application.acquired && s_wifi_state.radio_lease.acquired);
    ap_adoptions++;return 0;
}
'''
        code += fixture.extract(fixture.WIFI.read_text(),'wifi_adopt_ap_stop_cleanup')
        compile_run(self,code+r'''
int main(void) {
    s_wifi_ap_stop_cleanup=true;s_wifi_lifecycle.identity=41;
    s_wifi_state.initialized=true;s_wifi_state.started=true;s_wifi_state.status.connected=true;
    s_wifi_application.acquired=true;s_wifi_state.radio_lease.acquired=true;
    esp32_mquickjs_deinit_wifi_runtime(NULL);
    assert(!s_wifi_runtime && disconnect_calls==1 && !ap_adoptions && !configuration_cleanup_calls);
    assert(s_wifi_application.acquired && s_wifi_state.radio_lease.acquired && s_wifi_ap_stop_cleanup);
    const esp32_mquickjs_wifi_driver_event_t disconnected={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
    wifi_process_driver_event(&disconnected);
    assert(wifi_finish_runtime_cleanup(false)==ESP_ERR_INVALID_STATE && !ap_adoptions && posts==1);
    const esp32_mquickjs_wifi_driver_event_t drained={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
    wifi_process_driver_event(&drained);
    configuration_cleanup_error=-8;
    assert(wifi_finish_runtime_cleanup(false)==-8 && ap_adoptions==1 && configuration_cleanup_calls==1);
    assert(!s_wifi_ap_stop_cleanup && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41);
    assert(!s_wifi_configuration_stop_only && !s_wifi_configuration_disconnect_pending);
    configuration_cleanup_error=0;
    assert(wifi_finish_runtime_cleanup(false)==0 && ap_adoptions==1 && configuration_cleanup_calls==2);
    assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_lifecycle.identity && disconnect_calls==1);
}
''')
