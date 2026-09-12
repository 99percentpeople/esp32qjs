"""Production APSTA stopAP coordinator; test execution deferred to Wi-Fi phase.

Radio and netif boundaries are injected here. Their full production translation
units have separate SDK/scheduler fixtures. No alternative coordinator model.
"""
import re
import unittest
import test_wifi_configuration_cleanup as fixture
from test_wireless_control_regression import compile_run


class WiFiAPStop(unittest.TestCase):
    def code(self):
        code = fixture.WiFiConfigurationCleanup().code()
        # Use the real shared cleanup exports instead of the independent-AP stubs.
        for name in ['esp32_mquickjs_wifi_ap_stop_pending', 'esp32_mquickjs_wifi_stop_ap_shared']:
            code = re.sub(r'static ([^\n{};]*\b' + name + r'\([^\n]*?\)) \{[^\n]*\}',
                          r'\1;', code, count=1)
        code = code.replace('assert(native_token.identity && !running);',
                            'assert(native_token.identity && (!running || partial_quiesced));')
        code = code.replace('static bool expect_stop_only;', 'static bool expect_stop_only,partial_quiesced;')
        # The native retire helper may time out without releasing its storage.
        code = code.replace('ap_calls++;if(fail_at==2)return -2;',
                            'ap_calls++;if(fail_at==2)return -2;')
        code += BOUNDARIES
        ap, wifi = fixture.AP.read_text(), fixture.WIFI.read_text()
        code += ''.join(fixture.function(ap, n) for n in [
            'esp32_mquickjs_wifi_ap_control_lease', 'wifi_ap_partial_token',
            'esp32_mquickjs_wifi_ap_begin_partial_stop', 'esp32_mquickjs_wifi_ap_retire_partial_stop',
            'esp32_mquickjs_wifi_ap_finish_partial_stop', 'esp32_mquickjs_wifi_ap_adopt_partial_stop'])
        code += ''.join(fixture.function(wifi, n) for n in [
            'esp32_mquickjs_wifi_ap_stop_pending', 'esp32_mquickjs_wifi_stop_ap_shared',
            'wifi_adopt_ap_stop_cleanup', 'wifi_stop_idle'])
        code += fixture.function(ap, 'js_wifi_stop_ap')
        return code + SETUP

    def test_stop_ap_preserves_station_and_retries_only_unfinished_resources(self):
        compile_run(self, self.code() + r'''
int main(void) {
    for(int stage=0;stage<4;stage++) {
        setup();partial_fail=stage;
        if(stage) {
            assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);
            assert(s_wifi_ap_stop_cleanup && s_ap_partial_stop && s_ap_cleanup_pending);
            assert(s_wifi_lifecycle.identity==41 && s_ap_coordinator.identity==41);
            assert(s_wifi_application.identity==1 && s_wifi_state.radio_lease.identity==2 && s_ap_lease.acquired);
            assert(s_wifi_state.status.connected && sta_storage && !sta_calls && !stop_calls);
            assert(!owner_releases && !s_wifi_state.runtime_cleanup_pending);
            assert(s_ap_netif==(stage==3?NULL:(void *)1));
            partial_fail=0;fail_at=0;
        }
        assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123);
        assert(!s_wifi_ap_stop_cleanup && !s_ap_partial_stop && !s_ap_cleanup_pending);
        assert(!s_wifi_lifecycle.identity && !s_ap_coordinator.identity && !s_ap_lease.acquired && !s_ap_netif);
        assert(s_wifi_application.identity==1 && s_wifi_state.radio_lease.identity==2);
        assert(s_wifi_state.status.connected && sta_storage && running && !sta_calls && !stop_calls && !shutdown_calls);
        assert(owner_releases==1 && partial_admissions==1 && partial_mutations==1);
        int calls=partial_quiesce_calls;
        assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123 && partial_quiesce_calls==calls);
        assert(owner_releases==1 && !stop_calls && !shutdown_calls);
    }
}
''')

    def test_busy_and_foreign_admission_does_not_poison_the_ap(self):
        compile_run(self, self.code() + r'''
int main(void) {
    setup();
    bool *busy[]={&s_wifi_state.scan_in_progress,&s_wifi_state.scan_draining,
        &s_wifi_state.scan_results_pending,&s_wifi_state.scan_start_active,&s_wifi_state.scan_stop_active,
        &s_wifi_state.connect_in_progress,&s_wifi_state.connect_draining,&s_wifi_state.connect_start_active,
        &s_wifi_state.disconnect_active,&s_wifi_state.scan_future_registered,&s_wifi_state.connect_future_registered,
        &s_wifi_state.runtime_cleanup_pending};
    for(unsigned i=0;i<sizeof(busy)/sizeof(*busy);i++) {
        *busy[i]=true;assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);*busy[i]=false;
        assert(!s_ap_cleanup_pending && !s_wifi_ap_stop_cleanup && !s_wifi_lifecycle.identity);
        assert(!partial_admissions && !owner_releases && s_ap_netif && s_ap_lease.acquired);
    }
    foreign_owner=true;assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);
    assert(!s_ap_cleanup_pending && !s_wifi_ap_stop_cleanup && !s_wifi_lifecycle.identity && s_ap_lease.acquired);
    foreign_owner=false;
    assert(js_wifi_stop_ap(NULL,NULL,2,NULL)==-98 && partial_admissions==1);
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123 && partial_admissions==2);
}
''')

    def test_failed_partial_close_hands_same_token_to_whole_stop_after_station_exits(self):
        compile_run(self, self.code() + r'''
int main(void) {
    for(int stage=1;stage<4;stage++) {
        setup();partial_fail=stage;
        assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);
        assert(wifi_stop_idle()==ESP_ERR_INVALID_STATE && !adoptions && !stop_calls && !owner_releases);
        s_wifi_state.status.connected=false;s_wifi_state.connect_draining=true;
        assert(wifi_stop_idle()==ESP_ERR_INVALID_STATE && !adoptions);
        s_wifi_state.connect_draining=false;partial_fail=0;fail_at=1;
        int modes=partial_mutations;
        assert(wifi_stop_idle()==-1);
        assert(adoptions==1 && !s_wifi_ap_stop_cleanup && !s_ap_partial_stop);
        assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41 && s_ap_coordinator.identity==41);
        assert(owner_releases==3 && s_wifi_state.runtime_cleanup_pending && !s_wifi_configuration_stop_only);
        assert(partial_mutations==modes);
        fail_at=0;
        assert(wifi_stop_idle()==0 && adoptions==1 && !s_wifi_lifecycle.identity);
        assert(!s_wifi_configuration_cleanup && !s_wifi_state.runtime_cleanup_pending);
        assert(!s_ap_coordinator.identity && !s_ap_cleanup_pending && !s_ap_netif && !sta_storage);
        assert(owner_releases==3 && shutdown_calls==1 && partial_mutations==modes);
    }
}
''')

    def test_stale_tokens_and_permanent_detach_failure_keep_storage(self):
        compile_run(self, self.code() + r'''
int main(void) {
    setup();partial_fail=1;assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);
    esp32_mquickjs_wifi_radio_lifecycle_t stale=s_wifi_lifecycle;stale.generation++;
    assert(esp32_mquickjs_wifi_ap_retire_partial_stop(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_ap_finish_partial_stop(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_ap_adopt_partial_stop(&stale)==ESP_ERR_INVALID_STATE && !adoptions);
    partial_fail=0;s_ap_detach_error=-77;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99 && !ap_calls);
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99 && !ap_calls);
    assert(s_ap_netif && s_ap_lease.acquired && s_wifi_ap_stop_cleanup && s_wifi_state.status.connected);
    s_wifi_state.status.connected=false;
    assert(wifi_stop_idle()==-77 && adoptions==1 && stop_calls==1 && !sta_calls);
    assert(s_ap_netif && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41);
    assert(wifi_stop_idle()==-77 && adoptions==1 && stop_calls==1 && !shutdown_calls);
}
''')


BOUNDARIES = r'''
#define ESP_ERR_INVALID_ARG -21
#define WIFI_MODE_APSTA 3
static bool s_wifi_ap_stop_cleanup,s_ap_partial_stop;
static int partial_fail,partial_admissions,partial_quiesce_calls,partial_mutations,adoptions;
static bool partial_submitted;
static int esp32_mquickjs_wifi_radio_begin_ap_stop(const esp32_mquickjs_wifi_radio_lease_t *app,
    const esp32_mquickjs_wifi_radio_lease_t *sta,const esp32_mquickjs_wifi_radio_lease_t *ap,
    esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    partial_admissions++;assert(app->identity==1 && sta->identity==2 && ap->identity==3);
    return esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
}
static int esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(exact(t));partial_quiesce_calls++;
    if(!partial_submitted){partial_mutations++;partial_submitted=true;}
    if(partial_fail==1)return -11;
    partial_quiesced=true;fail_at=partial_fail==2?2:0;return 0;
}
static int esp32_mquickjs_wifi_radio_check_ap_stopped_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    return exact(t) && partial_quiesced?0:ESP_ERR_INVALID_STATE;
}
static int esp32_mquickjs_wifi_radio_finish_ap_stop(esp32_mquickjs_wifi_radio_lifecycle_t *t,
    esp32_mquickjs_wifi_radio_lease_t *ap) {
    assert(exact(t) && partial_quiesced && !s_ap_netif && ap==&s_ap_lease);
    if(partial_fail==3)return -13;
    esp32_mquickjs_wifi_radio_release(ap);
    memset(t,0,sizeof(*t));memset(&native_token,0,sizeof(native_token));return 0;
}
static int esp32_mquickjs_wifi_radio_adopt_ap_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(exact(t) && s_wifi_application.identity==1 && s_wifi_state.radio_lease.identity==2 && s_ap_lease.identity==3);
    adoptions++;return 0;
}
static int wifi_cleanup_failed_init(void) { assert(0);return -1; }
typedef int JSContext,JSValue;
#define JS_UNDEFINED -1
#define JS_EXCEPTION -99
static bool timeout_capture_error;
static uint32_t timeout_budget=1000;
static int wait_scope,wait_begins,wait_ends,wait_begin_error;
static bool esp32_mquickjs_wifi_capture_stop_ap_timeout(JSContext *ctx,JSValue value,uint32_t *out) {
    (void)ctx;assert(value==JS_UNDEFINED);if(timeout_capture_error)return false;*out=timeout_budget;return true;
}
static int esp32_mquickjs_wifi_wait_begin(uint32_t timeout) {
    assert(timeout==timeout_budget && !wait_scope);wait_begins++;if(wait_begin_error)return wait_begin_error;wait_scope=1;return 0;
}
static void esp32_mquickjs_wifi_wait_end(void) { assert(wait_scope);wait_scope=0;wait_ends++; }

static int JS_ThrowTypeError(JSContext *c,const char *m) { (void)c;(void)m;return -98; }
static int esp32_mquickjs_wifi_make_status_object(JSContext *c) { (void)c;return 123; }
static int wifi_ap_error(JSContext *c,const char *op,int err) { (void)c;assert(!strcmp(op,"wifi.stopAP") && err);return -99; }
'''

SETUP = r'''
static void setup(void) {
    memset(&s_wifi_state,0,sizeof(s_wifi_state));
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_ap_netif=(void *)1;s_wifi_state.status.connected=true;running=sta_storage=true;
    partial_fail=partial_admissions=partial_quiesce_calls=partial_mutations=adoptions=0;
    partial_quiesced=partial_submitted=false;
    begin_calls=owner_releases=stop_calls=ap_calls=sta_calls=shutdown_calls=fail_at=0;
}
'''
