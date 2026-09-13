"""Deferred production AP helper coordinator with actual Radio rate handoff."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.tx.test_wifi_raw_tx_ap_rate import ap_rate_code, MAIN as NATIVE_MAIN
from tests.c.integration.wifi.tx.test_wifi_tx_rate import COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiRawTxApHelper(unittest.TestCase):
    def test_helper_partial_open_close_suffix_and_recovery_replacement(self):
        for profile in ('esp32c3/representative','esp32s3/representative-psram','esp32c5/representative'):
            code = ap_rate_code(profile)
            code += structure((COMPONENT/'internal/esp32_mquickjs_wifi_raw_tx_ap.h').read_text(),
                              'esp32_mquickjs_wifi_raw_tx_ap_context_t')
            code += BOUNDARIES
            source = (COMPONENT/'src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
            for name in ('esp32_mquickjs_wifi_ap_open_raw_tx_rate','esp32_mquickjs_wifi_ap_close_raw_tx_rate'):
                code += extract(source,name)
            code += NATIVE_MAIN[:NATIVE_MAIN.index('int main(void)')]
            with self.subTest(profile=profile):compile_run(self,code+MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_ap_helper/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_ap_helper/main.inc')
