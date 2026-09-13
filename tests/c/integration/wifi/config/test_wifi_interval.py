"""Deferred production connectionless interval ledger; no Radio/ESP-NOW integration claim."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run


class WiFiInterval(unittest.TestCase):
    def test_real_writer_unknown_baseline_exact_owner_restore_suffix_and_exhaustion(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_interval.h')
        code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c')
        compile_run(self, code + MAIN)

    def test_real_frozen_capture_replay_rebuild_and_revision_capacity(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_interval.h')
        code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c')
        compile_run(self, code + REPLAY_MAIN)


PRELUDE = fixture_text('wifi/config/test_wifi_interval/prelude.inc')

MAIN = fixture_text('wifi/config/test_wifi_interval/main.inc')


REPLAY_MAIN = fixture_text('wifi/config/test_wifi_interval/replay_main.inc')
