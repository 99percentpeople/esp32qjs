"""Deferred production broadcast discovery Radio/Future and real VM conversion.

SDK read and background-worker scheduling are injected boundaries. Native
discovery/dispatch are exercised in test_wifi_twt_sdk. This does not replace
Future-core teardown or actual native-task/RF evidence. AST only this wave.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_radio import radio_code, MAIN as RADIO_MAIN, SOURCE as RADIO_SOURCE, INTERNAL
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def discovery_code():
    source = (COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast.c').read_text()
    code = radio_code(True) + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
    code += '#define ESP_ERR_TIMEOUT 99\n#define ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS 6000U\n'
    header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
    for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
    code += BOUNDARIES
    code += extract(RADIO_SOURCE.read_text(), 'esp32_mquickjs_wifi_radio_twt_broadcast_snapshot')
    code += source[source.index('struct esp32_mquickjs_future_driver_state {'):source.index('#define SET')]
    code += (CORE / 'esp32_mquickjs_options.c').read_text()
    code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include "')) + '\n'
    code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
    code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
    for name in ('broadcast_error', 'broadcast_capture', 'broadcast_worker', 'broadcast_schedule',
                 'broadcast_poll', 'broadcast_finish', 'broadcast_cancel', 'broadcast_timeout'):
        code += extract(source, name)
    # One-line release function is copied separately (extract expects multiline bodies).
    code += re.search(r'^static void broadcast_destroy\(.*$', source, re.M).group(0) + '\n'
    return code


class WiFiTwtBroadcast(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = build(cls.temp.name, discovery_code(), MAIN)

    def test_capture_gc_and_nth_allocation_failure(self):
        cases = [('undefined', 1), ('({})', 1), ('null', 0), ('[]', 0),
                 ('({timeoutMs:1})', 1), ('({timeoutMs:60000})', 1), ('({timeoutMs:60001})', 0),
                 ('({timeoutMs:0})', 0), ('({timeoutMs:1.5})', 0), ('({timeoutMs:"2"})', 0),
                 ('({timeoutMs:NaN})', 0), ('({timeoutMs:Infinity})', 0), ('({extra:1})', 0),
                 ('({get timeoutMs(){gc();return 12;}})', 1),
                 ('({get timeoutMs(){gc();throw 12345;}})', 0)]
        for value, valid in cases:
            with self.subTest(value=value):
                run([str(self.binary), 'capture', value, str(valid)])

    def test_snapshot_conversion_maximum_interval_and_allocation_failure(self):
        for mode in ('result', 'error'):
            run([str(self.binary), mode, 'undefined', '1'])

    def test_worker_saturation_cancel_timeout_and_radio_admission(self):
        run([str(self.binary), 'lifecycle', 'undefined', '1'])


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_broadcast/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_broadcast/main.inc')
