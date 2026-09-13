"""Deferred production iTWT setup/dwell timer adapter with controlled scheduling.

Real timer identity/storage/error code; SDK table capture/match and esp_timer are
injected. The SDK fixture separately exercises real capture/match. The packed
Host ETSTimer preserves 20-byte spacing with a Host pointer, not C5 field ABI.
Do not import, compile or execute before the Wi-Fi API stage.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiTwtSetupTimer(unittest.TestCase):
    def test_reuse_phase_queue_ordering_errors_and_foreign_timers(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_setup_timer.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_setup_timer.c')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/twt/test_wifi_twt_setup_timer/prelude.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_setup_timer/main.inc')
