"""Deferred production TWT management TX observer scheduling at native boundaries.

The actual wrappers/ledger are compiled when the Wi-Fi stage runs. Only driver
calls, allocation and locks are substituted. Host pointer width is not SDK ABI
evidence; C5 archive and final ELF inspection provide that separately.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiTwtTx(unittest.TestCase):
    def test_cached_early_recycle_reused_address_capacity_and_sticky_faults(self):
        code = PRELUDE
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_result.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_result.c')
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_tx.h')
        # A 64-bit Host metadata pointer at EB+56 overlaps the native C5
        # broadcast byte at 61. Relocate only that byte in the Host fixture;
        # production C5 field offsets are verified by the target build/ELF.
        tx = unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_tx.c')
        code += tx.replace('((const uint8_t *)buffer)[61]', '((const uint8_t *)buffer)[64]')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/twt/test_wifi_twt_tx/prelude.inc') + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information_timer.h')
PRELUDE += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_information.h')
PRELUDE += fixture_text('wifi/twt/test_wifi_twt_tx/fragment.inc')
PRELUDE += fixture_text('wifi/twt/test_wifi_twt_tx/fragment-02.inc')
PRELUDE += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_information.c')
MAIN = fixture_text('wifi/twt/test_wifi_twt_tx/main.inc')
