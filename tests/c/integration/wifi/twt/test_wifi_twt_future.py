"""Deferred production probe capture/conversion/worker callbacks with real VM.

Uses real Radio lease glue. Only worker scheduling and SDK/retirement boundaries
are controlled. Direct callbacks do not replace full Future-core scheduler or
ESP32 GC/RF qualification. Do not import, compile or execute this wave.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_radio import radio_code, MAIN as RADIO_MAIN, INTERNAL
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import CORE, build, extract, run

SOURCE = COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt.c'


def future_code():
    source = SOURCE.read_text()
    code = radio_code(True) + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
    code += '#define ESP_ERR_TIMEOUT 99\n#define ESP32_MQUICKJS_WIFI_TWT_DEFAULT_RESPONSE_MS 5000U\n#define ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS 6000U\n'
    information = (INTERNAL / 'esp32_mquickjs_wifi_twt_information.h').read_text()
    code += re.search(r'^#define ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS.*$', information, re.M).group(0) + '\n'
    code += structure(information, 'esp32_mquickjs_wifi_twt_information_result_t')
    code += fixture_text('wifi/twt/test_wifi_twt_future/future_code.inc')
    header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
    for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
    code += 'bool esp32_mquickjs_wifi_twt_agreement_service(void) {return false;}\nbool esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy(void) {return true;}\n'
    code += BOUNDARIES
    start = source.index('struct esp32_mquickjs_future_driver_state {')
    code += source[start:source.index('#define SET', start)]
    code += (CORE / 'esp32_mquickjs_options.c').read_text().replace('#include "esp32_mquickjs_options.h"', '')
    # Options may gain includes: use the same include-stripping helper as other VM fixtures.
    code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include "')) + '\n'
    code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
    code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
    code += extract(source, 'twt_information_value')
    for name in ('twt_probe_status_name', 'twt_error', 'twt_capture', 'twt_submit_worker', 'twt_schedule',
                 'twt_poll', 'twt_finish', 'twt_cancel', 'twt_destroy', 'twt_on_timeout',
                 'js_wifi_twt_status', 'js_wifi_twt_capabilities'):
        code += extract(source, name)
    return code


class WiFiTwtFuture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = build(cls.temp.name, future_code(), MAIN)

    def test_capture_and_conversion_nth_failure_rooting(self):
        cases = [('undefined', 1), ('({})', 1), ('null', 0), ('[]', 0),
                 ('({responseTimeoutMs:1,timeoutMs:60000})', 1),
                 ('({responseTimeoutMs:0})', 0), ('({responseTimeoutMs:60001})', 0),
                 ('({timeoutMs:0})', 0), ('({timeoutMs:1.5})', 0), ('({timeoutMs:"2"})', 0),
                 ('({extra:1})', 0), ('({timeoutMs:NaN})', 0), ('({timeoutMs:Infinity})', 0),
                 ('({get responseTimeoutMs(){gc();return 4;},get timeoutMs(){gc();return 8;}})', 1),
                 ('({get timeoutMs(){gc();throw 12345;}})', 0)]
        for value, valid in cases:
            with self.subTest(value=value):
                run([str(self.binary), 'capture', value, str(valid)])
        for mode in ('result', 'error', 'status', 'capabilities'):
            run([str(self.binary), mode, 'undefined', '1'])

    def test_cancel_publication_queue_saturation_cleanup_and_runtime_gate(self):
        run([str(self.binary), 'lifecycle', 'undefined', '1'])


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_future/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_future/main.inc')
