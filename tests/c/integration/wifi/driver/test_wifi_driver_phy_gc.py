"""Deferred production PHY converter/interface parsing with moving GC and OOM."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest

from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT, phy_types
from tests.support.wireless_vm_fixture import build, extract, run


class WiFiDriverPhyGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code = phy_types('esp32c5/representative')
        for name in ('tx_rate_interface', 'driver_protocols_to_js', 'driver_phy_to_js'):
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_all_shapes_gc_oom_and_interface_strings(self):
        for case in ('protocol', 'protocols2', 'protocols5', 'protocolsBoth',
                     'bandwidth', 'bandwidths2', 'bandwidths5', 'bandwidthsBoth'):
            with self.subTest(case=case):
                run([str(self.binary), case])


MAIN = fixture_text('wifi/driver/test_wifi_driver_phy_gc/main.inc')
