"""Deferred Action public capture/result tests against the production adapter.

Uses the real MQuickJS/ByteView and production option/copy/converter functions.
Allocation boundaries inject Nth failures and moving GC. No SDK/RF behavior or
Future scheduler/Radio concurrency is simulated by this fixture.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_action.c'


class WiFiActionCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text()
        code = fixture_text('wifi/tx/test_wifi_action_capture_gc/setupclass-code.inc')
        code += sdk_types('esp32c5/representative', ('wifi_action_tx_req_t', 'wifi_action_tx_status_type_t', 'wifi_roc_req_t'))
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_action_lane.h')
        code += structure((INTERNAL / 'esp32_mquickjs_wifi_roc_session.h').read_text(), 'esp32_mquickjs_wifi_roc_status_t')
        code += structure(source, 'action_options_t')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += BOUNDARIES
        code += unit(INTERNAL / "esp32_mquickjs_native_status.h")
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ('action_hex', 'action_mac', 'action_capture_options', 'action_capture_payload',
                     'action_capture', 'action_finish', 'js_wifi_action_capabilities',
                     'esp32_mquickjs_wifi_action_status'):
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_payload_options_gc_allocation_and_throwing_getters(self):
        base = 'channel:6,destination:"02:00:00:00:00:01",'
        cases = [
            ('({' + base + 'payload:view})', 1),
            ('({' + base + 'payload:[4],waitMs:60000,timeoutMs:1,noAck:true})', 1),
            ('({' + base + 'payload:[4],interface:"access-point",bssid:"FF:ff:ff:ff:ff:ff",secondaryChannel:"above"})', 1),
            ('({channel:6,destination:"00:00:00:00:00:00",payload:[4]})', 0),
            ('({channel:6,destination:"02:00:00:00:00:01\\u0000",payload:[4]})', 0),
            ('({' + base + 'payload:[]})', 0),
            ('({' + base + 'payload:[256]})', 0),
            ('({' + base + 'payload:[1.5]})', 0),
            ('({' + base + 'payload:{length:4294967295}})', 0),
            ('({' + base + 'payload:{length:1477}})', 0),
            ('({' + base + 'payload:view,timeoutMs:0})', 0),
            ('({' + base + 'payload:view,waitMs:60001})', 0),
            ('({' + base + 'payload:view,noAck:1})', 0),
            ('({' + base + 'payload:view,extra:true})', 0),
            ('({' + base + 'payload:view,interface:"station\\u0000"})', 0),
            ('({' + base + 'payload:view,get waitMs(){view.close();return 100;}})', 0),
            ('({' + base + 'get payload(){throw new Error("sentinel");}})', 0),
            ('({' + base + 'payload:{get length(){throw new Error("sentinel");}}})', 0),
            ('({' + base + 'payload:{length:1,get 0(){throw new Error("sentinel");}}})', 0),
            ('(function(){var n=0;return {' + base + 'get payload(){if(++n!==1)throw new Error("twice");return [4];}};})()', 1),
            ('(function(){var a=[];for(var i=0;i<1476;i++)a.push(4);return {' + base + 'payload:a};})()', 1),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected),
                     'sentinel' if 'sentinel' in expression else ''])

    def test_result_status_and_capabilities_gc_allocation(self):
        for outcome in ('failed', 'absent'):
            run([str(self.binary), 'finish', outcome, '1', ''])
        for mode in ('finish', 'status', 'capabilities'):
            with self.subTest(mode=mode):
                run([str(self.binary), mode, '', '1', ''])


BOUNDARIES = fixture_text('wifi/tx/test_wifi_action_capture_gc/boundaries.inc')
MAIN = fixture_text('wifi/tx/test_wifi_action_capture_gc/main.inc')
