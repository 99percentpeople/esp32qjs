"""Production stop capture and shared budget/netif scheduling. Phase run deferred."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
import tests.c.integration.wifi.station.test_wifi_netif_retirement as netif_fixture

WAIT = ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_wait.c'
CONFIG = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c'


def wait_source():
    return '\n'.join(line for line in WAIT.read_text().splitlines() if not line.startswith('#include '))


WAIT_BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_stop_timeout/wait_boundaries.inc')


class WiFiStopWait(unittest.TestCase):
    def code(self):
        return netif_fixture.WiFiNetifRetirement().code(WAIT_BOUNDARIES + wait_source())

    def test_shared_budget_across_two_netifs_retains_late_cleanup_suffix(self):
        compile_run(self, self.code() + fixture_text('wifi/lifecycle/test_wifi_stop_timeout/test_shared_budget_across_two_netifs_retains_late_cleanup_suffix.inc'))

    def test_task_isolation_nested_admission_expiration_wrap_and_tick_rounding(self):
        code = self.code().replace('#define configTICK_RATE_HZ 1000', '#define configTICK_RATE_HZ 100')
        compile_run(self, code + fixture_text('wifi/lifecycle/test_wifi_stop_timeout/test_task_isolation_nested_admission_expiration_wrap_and_tick_rounding.inc'))

    def test_large_budget_rejects_tick_conversion_overflow_before_admission(self):
        code = self.code().replace('#define configTICK_RATE_HZ 1000', '#define configTICK_RATE_HZ 10000')
        compile_run(self, code + fixture_text('wifi/lifecycle/test_wifi_stop_timeout/test_tick_overflow.inc'))


class WiFiStopCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        capture = ''.join(extract(CONFIG.read_text(), name) for name in (
            'wifi_capture_lifecycle_timeout', 'esp32_mquickjs_wifi_capture_stop',
            'esp32_mquickjs_wifi_capture_stop_ap_timeout', 'esp32_mquickjs_wifi_capture_disconnect'))
        future = CONFIG.with_name('esp32_mquickjs_wifi_future.c').read_text()
        a = future.index('typedef enum {'); b = future.index('} wifi_future_kind_t;', a) + len('} wifi_future_kind_t;')
        start = future.index('struct esp32_mquickjs_future_driver_state {')
        end = future.index('\n};', start) + len('\n};')
        boundary = fixture_text('wifi/lifecycle/test_wifi_stop_timeout/disconnect_boundaries.inc')
        body = boundary + options + capture + future[a:b] + future[start:end]
        body += extract(future, 'wifi_disconnect_future_prepare')
        cls.binary = build(cls.temp.name, body, CAPTURE_MAIN)

    def test_strict_timeout_defaults_rooting_and_nth_allocation_failure(self):
        for expression, expected in [
            ('undefined', 1000), ('({})', 1000), ('({timeoutMs:undefined})', 1000),
            ('({timeoutMs:1})', 1), ('({timeoutMs:60000})', 60000), ('({timeoutMs:60001})', 60001), ('({timeoutMs:2147483647})', 2147483647),
            ('(function(){var n=0;return {get timeoutMs(){if(++n!==1)throw new Error("twice");return 123;}};})()', 123),
            ('({get timeoutMs(){throw new Error("sentinel");}})', 0),
            ('1000', 0), ('null', 0), ('[]', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:-1})', 0),
            ('({timeoutMs:2147483648})', 0), ('({timeoutMs:1.5})', 0),
            ('({timeoutMs:4294967297})', 0), ('({timeoutMs:"100"})', 0),
            ('({timeoutMs:true})', 0), ('({timeoutMs:null})', 0),
            ('({timeoutMs:0/0})', 0), ('({timeoutMs:1/0})', 0),
            ('({"timeoutMs\\u0000":10})', 0), ('({force:true})', 0)]:
            for operation in (0, 1, 2):
                timeout = 15000 if operation == 2 and expected == 1000 else expected
                run([str(self.binary), expression, str(timeout), str(operation)])


CAPTURE_MAIN = fixture_text('wifi/lifecycle/test_wifi_stop_timeout/capture_main.inc')
