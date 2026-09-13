"""Deferred production information timer ownership and numeric native dispatch.

Reuses only the common allocator/timer type boundary prelude. Native capture
and timer scheduling are injected; SDK capture itself is covered separately.
Do not import, compile or run before the Wi-Fi API validation stage.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.twt.test_wifi_twt_setup_timer import PRELUDE
from tests.support.native_compile import compile_run


class WiFiTwtInformationTimer(unittest.TestCase):
    def test_payload_owner_cancel_reuse_early_dispatch_and_error_suffixes(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information_timer.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information.h')
        code += '\nbool esp32_mquickjs_wifi_twt_setup_request_closing(int16_t request_id);\n'
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_information_timer.c')
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/twt/test_wifi_twt_information_timer/main.inc')
