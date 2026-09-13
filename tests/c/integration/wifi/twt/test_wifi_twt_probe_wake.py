"""Deferred production probe PM ownership, including retained-timer stop.

No import/compile/execution until the Wi-Fi stage. SDK PM calls and locks are
controlled; the ownership helper is the production code used by six call sites.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiTwtProbeWake(unittest.TestCase):
    def test_repeated_release_does_not_consume_other_pm_owners(self):
        code = PRELUDE
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_wake.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_wake.c')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/twt/test_wifi_twt_probe_wake/prelude.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_probe_wake/main.inc')
