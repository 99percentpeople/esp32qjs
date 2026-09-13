"""Deferred production write-only policy ledger; not SDK/RF replay proof."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run


class WiFiPolicyRecord(unittest.TestCase):
    def test_real_record_acceptance_failure_history_invalidation_and_exhaustion(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_policy.h')
        code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_policy.c')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/config/test_wifi_policy_record/prelude.inc')

MAIN = fixture_text('wifi/config/test_wifi_policy_record/main.inc')
