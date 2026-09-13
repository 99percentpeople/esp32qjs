"""Deferred actual broadcast timer adapter with deterministic native scheduling.

Reuses the timer API boundary declarations; includes the production adapter.
Native capture/match is injected here and exercised separately in the SDK
fixture. No fixture import/compile/run until the Wi-Fi validation stage.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_setup_timer import PRELUDE
from tests.c.integration.wifi.twt.test_wifi_twt_broadcast_event import broadcast_types, BOUNDARIES
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiTwtBroadcastTimer(unittest.TestCase):
    def test_numeric_identity_copy_close_and_error_suffixes(self):
        code = PRELUDE + broadcast_types() + BOUNDARIES
        code += "void esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native(void);\nbool esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(uintptr_t);\n"
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_event.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_event.c')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_teardown_tx.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_tx.h')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_timer.h')
        code += fixture_text('wifi/twt/test_wifi_twt_broadcast_timer/test_numeric_identity_copy_close_and_error_suffixes.inc')
        code += "void esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(uintptr_t, uint8_t, const uint8_t *, uint8_t);\n"
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_timer.c')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_rx.c')
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/twt/test_wifi_twt_broadcast_timer/main.inc')
