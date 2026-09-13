"""Deferred production STA/AP pre-start rate transaction; AST only in API phase.

Radio lifecycle and AP helper admission are separate integration requirements.
The writer injects SDK effects; no replacement borrowing state machine is used.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.tx.test_wifi_tx_rate import rate_code
from tests.support.native_compile import compile_run


class WiFiTxRateBorrow(unittest.TestCase):
    def test_interface_predecessor_rollback_retained_owner_and_restore_suffix(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for softap in (False, True):
                with self.subTest(profile=profile, softap=softap):
                    code = rate_code(profile)
                    if not softap:
                        code = code.replace('#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1',
                                            '#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 0')
                    compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/tx/test_wifi_tx_rate_borrow/main.inc')
