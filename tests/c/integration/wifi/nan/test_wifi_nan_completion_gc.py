"""Production NAN result projections: observed bool vs Action enum, OOM and GC."""
import tempfile
import unittest
from tests.support.fixtures import fixture_text
from tests.support.wireless_vm_fixture import ROOT, INTERNAL, build, extract, run
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.config.test_wifi_config_controls import structure

class NanCompletionGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        module = ROOT / 'components/esp32_mquickjs/src/modules/wifi_nan'
        code = '#define WIFI_ACTION_TX_DONE 0\n#define WIFI_ACTION_TX_FAILED 1\ntypedef int esp_err_t;\n'
        for file, name in [('tx', 'esp32_mquickjs_wifi_nan_message_tx_status_t'), ('message', 'esp32_mquickjs_wifi_nan_message_status_t')]:
            code += structure((INTERNAL / ('esp32_mquickjs_wifi_nan_' + file + '.h')).read_text(), name)
        code += unit(INTERNAL / 'esp32_mquickjs_native_status.h')
        code += unit(INTERNAL / 'esp32_mquickjs_js_macros.h')
        code += '\n#define SET(o,n,v) ESP32_MQUICKJS_SET_OR_GOTO(ctx,o,n,v,fail)\n'
        code += extract((module / 'esp32_mquickjs_wifi_nan_service_public.inc').read_text(), 'nan_event_bytes')
        code += extract((module / 'esp32_mquickjs_wifi_nan_message_public.inc').read_text(), 'nan_message_result_to_js')
        code += fixture_text('wifi/nan/test_wifi_nan_completion_gc/boundaries.inc')
        code += extract((module / 'esp32_mquickjs_wifi_nan_message_public.inc').read_text(), 'nan_message_finish')
        cls.binary = build(cls.temp.name, code, fixture_text('wifi/nan/test_wifi_nan_completion_gc/main.inc'))

    def test_completion_absence_boolean_and_native_enum_with_gc(self):
        for usd, codes in [(0, (-1, 0, 1)), (1, (-1, 0, 1, 99))]:
            for code in codes:
                with self.subTest(usd=usd, code=code):
                    run([str(self.binary), str(code), str(usd)])
