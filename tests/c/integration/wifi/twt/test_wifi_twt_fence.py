"""Deferred production TWT timer/native queue ordering and storage retirement.

Only esp_timer and native ioctl outcomes/scheduling are replaced. No alternate
TWT lifecycle, no fixture import/compile/run during API implementation.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.tx.test_wifi_action_lane import PRELUDE
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.native_compile import compile_run


class WiFiTwtFence(unittest.TestCase):
    def test_early_callback_cancel_late_exit_exact_revision_and_failed_suffix(self):
        lane = (COMPONENT / 'internal/esp32_mquickjs_wifi_twt_lane.h').read_text()
        code = PRELUDE + '\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#include <stdatomic.h>\n'
        code += structure(lane, 'esp32_mquickjs_wifi_twt_token_t')
        code += BOUNDARIES
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_fence.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_fence.c')
        compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_fence/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_fence/main.inc')
