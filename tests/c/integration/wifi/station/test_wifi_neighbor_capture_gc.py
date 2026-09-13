"""Deferred production Neighbor Report options/conversion with real VM GC/OOM.

The report producer is injected; allocation, ownership, chunk reads and decoded
fields are production code. Does not stand in for SDK/Radio/Future scheduling.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

MODULE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_roaming'


class WiFiNeighborCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.temp.cleanup)
        source = (MODULE / 'esp32_mquickjs_wifi_neighbor_request.c').read_text()
        native = (MODULE / 'esp32_mquickjs_wifi_rrm_request.c').read_text()
        radio = (INTERNAL / 'esp32_mquickjs_wifi_radio.h').read_text()
        code = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_RRM_SUPPORT 1\n'
        code += fixture_text('wifi/station/test_wifi_neighbor_capture_gc/setupclass.inc')
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', radio).group(0)
        code += structure(radio, 'esp32_mquickjs_wifi_radio_operation_t')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_rrm_sdk.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_rrm_request.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_neighbor.h')
        code += BOUNDARIES
        code += native[native.index('struct esp32_mquickjs_wifi_rrm_request {'):native.index('static void rrm_free_report')]
        for name in ['rrm_free_report', 'rrm_detach_report_locked', 'esp32_mquickjs_wifi_rrm_retain',
                     'esp32_mquickjs_wifi_rrm_release', 'esp32_mquickjs_wifi_rrm_create', 'rrm_cancel_locked',
                     'esp32_mquickjs_wifi_rrm_close', 'esp32_mquickjs_wifi_rrm_status', 'esp32_mquickjs_wifi_rrm_copy']:
            code += extract(native, name)
        code += unit(MODULE.parent / 'wifi_common/esp32_mquickjs_wifi_neighbor.c')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ['neighbor_terminal', 'neighbor_snapshot', 'neighbor_error', 'neighbor_options', 'neighbor_report']:
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_options_getters_and_report_gc_allocation_failures(self):
        for value, valid in [('({})', 1), ('undefined', 1), ('null', 0), ('[]', 0),
                             ('({maxReportBytes:4096,timeoutMs:60000})', 1),
                             ('({maxReportBytes:0})', 0), ('({maxReportBytes:4097})', 0),
                             ('({maxReportBytes:1.5})', 0), ('({maxReportBytes:"10"})', 0),
                             ('({timeoutMs:0})', 0), ('({timeoutMs:60001})', 0), ('({extra:1})', 0),
                             ('({get timeoutMs(){gc();return 2;},get maxReportBytes(){gc();return 32;}})', 1),
                             ('({get maxReportBytes(){gc();throw new Error("sentinel");}})', 0)]:
            with self.subTest(value=value):run([str(self.binary), 'capture', value, str(valid)])
        for mode in ['report', 'status', 'truncated', 'duplicate-preference', 'too-many']:
            run([str(self.binary), mode, '', '1'])


BOUNDARIES = fixture_text('wifi/station/test_wifi_neighbor_capture_gc/boundaries.inc')
MAIN = fixture_text('wifi/station/test_wifi_neighbor_capture_gc/main.inc')
