"""Deferred production timer identity guard with controlled SDK scheduling.

No fixture import, compile or execution during the Wi-Fi API implementation
wave. Native capture/match separately use the production SDK fixture.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiTwtProbeTimer(unittest.TestCase):
    def test_late_timer_queued_node_reuse_phase_changes_and_post_failure(self):
        code = PRELUDE
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_wake.h')
        code += fixture_text('wifi/twt/test_wifi_twt_probe_timer/test_late_timer_queued_node_reuse_phase_changes_and_post_failure.inc')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_timer.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_timer.c')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/twt/test_wifi_twt_probe_timer/prelude.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_probe_timer/main.inc')
