"""Deferred production probe result capture and nonblocking event publication.

Only SDK event posting/locks are controlled. Do not import, compile or execute
this fixture until the Wi-Fi API stage; AST checks are not runtime evidence.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiTwtProbeResult(unittest.TestCase):
    def test_early_completion_queue_failure_duplicates_and_identity(self):
        code = PRELUDE
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_result.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_result.c')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/twt/test_wifi_twt_probe_result/prelude.inc')
# Keep observation normalization in the production information implementation.
PRELUDE += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information_timer.h')
PRELUDE += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information.h')
PRELUDE += fixture_text('wifi/twt/test_wifi_twt_probe_result/fragment.inc')
PRELUDE += fixture_text('wifi/twt/test_wifi_twt_probe_result/fragment-02.inc')
PRELUDE += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_information.c')
MAIN = fixture_text('wifi/twt/test_wifi_twt_probe_result/main.inc')
