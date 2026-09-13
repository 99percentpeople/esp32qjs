"""Deferred production ROC option/status conversion with real moving-GC VM.

This fixture checks pure public capture/conversion. Native lifecycle is covered
by test_wifi_roc_session; full Future/VM/Radio integration remains phase testing.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_roc.c'


class WiFiRocCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text()
        code = fixture_text('wifi/tx/test_wifi_roc_capture_gc/setupclass-code.inc')
        code += sdk_types('esp32c5/representative', ('wifi_roc_req_t',))
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_action_lane.h')
        code += structure((INTERNAL / 'esp32_mquickjs_wifi_roc_session.h').read_text(), 'esp32_mquickjs_wifi_roc_status_t')
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += 'static int esp32_mquickjs_wifi_action_receive(uint8_t *a,uint8_t *b,size_t c,uint8_t d) {(void)a;(void)b;(void)c;(void)d;return 0;}\n'
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        code += extract(source, 'roc_options')
        code += extract(source, 'roc_status_to_js')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_options_strict_bounds_gc_single_getter_and_exceptions(self):
        cases = [
            ('({channel:6,durationMs:100})', 1),
            ('({channel:14,durationMs:60000,timeoutMs:1,interface:"access-point",secondaryChannel:"below",allowBroadcast:true})', 1),
            ('({channel:6,durationMs:0})', 0),
            ('({channel:6,durationMs:60001})', 0),
            ('({channel:6,durationMs:1.5})', 0),
            ('({channel:6,durationMs:100,timeoutMs:0})', 0),
            ('({channel:36,durationMs:100})', 0),
            ('({channel:4294967295,durationMs:100})', 0),
            ('({channel:6,durationMs:100,allowBroadcast:1})', 0),
            ('({channel:6,durationMs:100,interface:"station\\u0000"})', 0),
            ('({channel:6,durationMs:100,secondaryChannel:"above\\u0000"})', 0),
            ('({channel:6,durationMs:100,extra:true})', 0),
            ('null', 0), ('[]', 0),
            ('({channel:6,get durationMs(){throw new Error("sentinel");}})', 0),
            ('(function(){var n=0;return {channel:6,get durationMs(){if(++n!==1)throw new Error("twice");return 100;}};})()', 1),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected),
                     'sentinel' if 'sentinel' in expression else ''])

    def test_status_gc_and_each_conversion_allocation(self):
        run([str(self.binary), 'status', '', '1', ''])


MAIN = fixture_text('wifi/tx/test_wifi_roc_capture_gc/main.inc')
