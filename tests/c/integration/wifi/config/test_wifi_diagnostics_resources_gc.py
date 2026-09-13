"""Production Monitor/CSI result converters in the real VM; execution deferred.

Native ledger boundaries are injected here. Registry ownership and allocator
retirement are covered separately by the production Session/resource fixtures.
This fixture does not prove target struct sizes or concurrent SDK scheduling.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, build, extract, run


class WiFiDiagnosticsResourcesGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        base = ROOT / 'components/esp32_mquickjs/src/modules'
        monitor = (base / 'wifi_monitor/esp32_mquickjs_wifi_monitor.c').read_text()
        csi = (base / 'wifi_csi/esp32_mquickjs_wifi_csi.c').read_text()
        macros = monitor[monitor.index('#define SET('):monitor.index('\n\n', monitor.index('#define SET('))]
        code = BOUNDARIES + macros + '\n'
        code += extract(monitor, 'esp32_mquickjs_wifi_monitor_diagnostics')
        code += extract(csi, 'wifi_csi_lifecycle_name')
        code += extract(csi, 'wifi_csi_store_state_name')
        code += extract(csi, 'esp32_mquickjs_wifi_csi_diagnostics')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_native_values_are_copied_before_gc_and_each_allocation_can_fail(self):
        for state in range(5):
            for collect in (0, 1):
                run([str(self.binary), str(state), str(collect)])


BOUNDARIES = fixture_text('wifi/config/test_wifi_diagnostics_resources_gc/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_diagnostics_resources_gc/main.inc')
