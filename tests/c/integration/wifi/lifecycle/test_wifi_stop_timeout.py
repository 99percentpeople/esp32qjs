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
            'wifi_capture_lifecycle_timeout', 'esp32_mquickjs_wifi_capture_stop'))
        cls.binary = build(cls.temp.name, options + capture, CAPTURE_MAIN)

    def test_strict_timeout_defaults_rooting_and_nth_allocation_failure(self):
        for expression, expected in [
            ('undefined', 1000), ('({})', 1000), ('({timeoutMs:undefined})', 1000),
            ('({timeoutMs:1})', 1), ('({timeoutMs:60000})', 60000),
            ('null', 0), ('[]', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:-1})', 0),
            ('({timeoutMs:60001})', 0), ('({timeoutMs:1.5})', 0),
            ('({timeoutMs:4294967297})', 0), ('({timeoutMs:"100"})', 0),
            ('({timeoutMs:true})', 0), ('({timeoutMs:null})', 0),
            ('({timeoutMs:0/0})', 0), ('({timeoutMs:1/0})', 0),
            ('({"timeoutMs\\u0000":10})', 0), ('({force:true})', 0)]:
            run([str(self.binary), expression, str(expected)])


CAPTURE_MAIN = fixture_text('wifi/lifecycle/test_wifi_stop_timeout/capture_main.inc')
