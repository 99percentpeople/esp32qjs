"""Actual stopAP timeout capture and feature-disabled adapter; phase run deferred."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
from tests.c.integration.wifi.lifecycle.test_wifi_stop_timeout import WiFiStopCapture, CONFIG
from tests.support.native_compile import compile_run
import tests.c.integration.wifi.ap.test_wifi_ap_stop as ap_stop_fixture

AP = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c'


class WiFiStopAPCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        WiFiStopCapture.setUpClass.__func__(cls)
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        capture = ''.join(extract(CONFIG.read_text(), name) for name in (
            'wifi_capture_lifecycle_timeout', 'esp32_mquickjs_wifi_capture_stop_ap_timeout'))
        disabled = extract(AP.read_text().split('#elif CONFIG_ESP32_MQUICKJS_FEATURE_WIFI', 1)[1], 'js_wifi_stop_ap')
        cls.disabled = build(cls.temp.name + '/disabled', options + capture +
                             'static JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx) { (void)ctx;return JS_TRUE; }\n' + disabled,
                             DISABLED_MAIN)

    def test_options_timeout_strict_validation_and_exception_allocation_failures(self):
        for expression, expected in [
            ('undefined', 1000), ('({timeoutMs:1})', 1), ('({timeoutMs:2147483647})', 2147483647), ('({timeoutMs:60001})', 60001),
            ('0', 0), ('-1', 0), ('1.5', 0), ('60001', 0), ('4294967297', 0),
            ('null', 0), ('true', 0), ('"1000"', 0), ('({timeoutMs:10})', 10),
            ('[]', 0), ('0/0', 0), ('1/0', 0)]:
            run([str(self.binary), expression, str(expected), "1"])

    def test_disabled_stop_keeps_noop_status_and_validates_input(self):
        run([str(self.disabled)])


DISABLED_MAIN = fixture_text('wifi/lifecycle/test_wifi_stop_ap_timeout/disabled_main.inc')


class WiFiStopAPScope(unittest.TestCase):
    def test_capture_and_scope_admission_precede_mutation_and_failed_cleanup_ends_scope(self):
        compile_run(self, ap_stop_fixture.WiFiAPStop().code() + fixture_text('wifi/lifecycle/test_wifi_stop_ap_timeout/test_capture_and_scope_admission_precede_mutation_and_failed_cleanup_ends_scope.inc'))
