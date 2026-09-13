"""Deferred production Monitor metadata conversion with the real moving MQuickJS GC.

No alternate metadata implementation: compose the production RX parser and
converter, inject each JS allocation/property failure, and inspect actual JS.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, build, run
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.monitor.test_wifi_monitor_wire import production_monitor_wire


class WiFiMonitorMetadata(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        body = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
        body += production_monitor_wire(he=True)
        body += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_metadata.c')
        cls.binary = build(cls.temp.name, body, MAIN)

    def test_frame_capture_metadata_only_short_header_type_mismatch_and_unknown_phy(self):
        for scenario in range(16):
            with self.subTest(scenario=scenario): run([str(self.binary), str(scenario)])


MAIN = fixture_text('wifi/monitor/test_wifi_monitor_metadata/main.inc')
