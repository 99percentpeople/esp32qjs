"""Actual stopAP timeout capture and feature-disabled adapter; phase run deferred."""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, CORE, build, extract, run
from test_wifi_stop_timeout import CAPTURE_MAIN, CONFIG
from test_wireless_control_regression import compile_run
import test_wifi_ap_stop as ap_stop_fixture

AP = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c'


class WiFiStopAPCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.temp.cleanup)
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        capture = extract(CONFIG.read_text(), 'esp32_mquickjs_wifi_capture_stop_ap_timeout')
        cls.binary = build(cls.temp.name, options + capture, CAPTURE_MAIN.replace(
            'esp32_mquickjs_wifi_capture_stop', 'esp32_mquickjs_wifi_capture_stop_ap_timeout'))
        disabled = extract(AP.read_text().split('#elif CONFIG_ESP32_MQUICKJS_FEATURE_WIFI', 1)[1], 'js_wifi_stop_ap')
        cls.disabled = build(cls.temp.name + '/disabled', options + capture +
                             'static JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx) { (void)ctx;return JS_TRUE; }\n' + disabled,
                             DISABLED_MAIN)

    def test_scalar_timeout_strict_validation_and_exception_allocation_failures(self):
        for expression, expected in [
            ('undefined', 1000), ('1', 1), ('60000', 60000), ('37', 37),
            ('0', 0), ('-1', 0), ('1.5', 0), ('60001', 0), ('4294967297', 0),
            ('null', 0), ('true', 0), ('"1000"', 0), ('({timeoutMs:10})', 0),
            ('[]', 0), ('0/0', 0), ('1/0', 0)]:
            run([str(self.binary), expression, str(expected)])

    def test_disabled_stop_keeps_noop_status_and_validates_input(self):
        run([str(self.disabled)])


DISABLED_MAIN = r'''
int main(void) {
    void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
    assert(JS_IsBool(js_wifi_stop_ap(ctx,NULL,0,NULL)));
    JSValue timeout=JS_NewInt32(ctx,17);
    assert(JS_IsBool(js_wifi_stop_ap(ctx,NULL,1,&timeout)));
    timeout=JS_NULL;assert(JS_IsException(js_wifi_stop_ap(ctx,NULL,1,&timeout)));JS_GetException(ctx);
    assert(JS_IsException(js_wifi_stop_ap(ctx,NULL,2,NULL)));JS_GetException(ctx);
    JS_FreeContext(ctx);free(heap);
}
'''


class WiFiStopAPScope(unittest.TestCase):
    def test_capture_and_scope_admission_precede_mutation_and_failed_cleanup_ends_scope(self):
        compile_run(self, ap_stop_fixture.WiFiAPStop().code() + r'''
int main(void) {
    setup();timeout_capture_error=true;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==JS_EXCEPTION);
    assert(!wait_begins && !partial_admissions && s_ap_lease.acquired);
    timeout_capture_error=false;wait_begin_error=-67;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==JS_EXCEPTION);
    assert(!wait_ends && !partial_admissions && s_ap_lease.acquired);
    wait_begin_error=0;timeout_budget=37;partial_fail=1;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==JS_EXCEPTION);
    assert(wait_ends==1 && !wait_scope && s_wifi_ap_stop_cleanup && partial_mutations==1);
    unsigned station=s_wifi_state.radio_lease.identity,app=s_wifi_application.identity;
    partial_fail=0;
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123);
    assert(wait_ends==2 && !wait_scope && partial_mutations==1 && !s_wifi_ap_stop_cleanup);
    assert(s_wifi_state.radio_lease.identity==station && s_wifi_application.identity==app);
}
''')
