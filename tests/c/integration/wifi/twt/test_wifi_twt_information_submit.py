"""Deferred production information producer rollback, using the real TX ledger.

No fixture import, compilation or execution before Wi-Fi staged validation.
Production producer/TX/callback/recycler code covers PM rollback and identity
retention. This is not SDK execution or evidence of native queue/RF ordering.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.twt.test_wifi_twt_tx import PRELUDE, MAIN as TX_MAIN
from tests.support.native_compile import compile_run


class WiFiTwtInformationSubmit(unittest.TestCase):
    def test_no_output_raw_error_reentrancy_and_early_completion(self):
        code = '#define TEST_REAL_INFORMATION_SUBMIT 1\n' + PRELUDE
        for name in ('probe_result', 'tx'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_result.c')
        tx = unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_tx.c')
        code += tx.replace('((const uint8_t *)buffer)[61]', '((const uint8_t *)buffer)[64]')
        compile_run(self, code + TX_MAIN.split('int main(void) {')[0] + MAIN)


MAIN = fixture_text('wifi/twt/test_wifi_twt_information_submit/main.inc')
