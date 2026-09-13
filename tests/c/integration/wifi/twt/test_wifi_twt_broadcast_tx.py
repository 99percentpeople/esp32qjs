"""Deferred broadcast setup through the real management TX ledger/wrappers.

Original output/callback/recycle, association and timer availability are injected.
No fixture import, compilation or execution before Wi-Fi staged validation.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.twt.test_wifi_twt_tx import PRELUDE, MAIN as TX_MAIN
from tests.support.native_compile import compile_run


class WiFiTwtBroadcastTx(unittest.TestCase):
    def test_exact_body_close_duplicate_callback_and_storage_lifetime(self):
        code = PRELUDE
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_result.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_result.c')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_tx.h')
        tx = unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_tx.c')
        code += tx.replace('((const uint8_t *)buffer)[61]', '((const uint8_t *)buffer)[64]')
        compile_run(self, code + TX_MAIN.split('int main(void) {')[0] + MAIN)


MAIN = fixture_text('wifi/twt/test_wifi_twt_broadcast_tx/main.inc')
