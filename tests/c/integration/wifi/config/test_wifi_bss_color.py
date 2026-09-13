"""Deferred actual BSS-color Radio policy admission, errors and post-START replay."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_policy import policy_code
from tests.support.native_compile import compile_run


class WiFiBssColor(unittest.TestCase):
    def test_exact_owners_native_error_target_gate_and_replay(self):
        for he in (False, True):
            code = f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(he)}\n' + policy_code('esp32c5/representative', True, True)
            marker = 'static esp_err_t wifi_radio_policy_writer('
            boundary = fixture_text('wifi/config/test_wifi_bss_color/test_exact_owners_native_error_target_gate_and_replay-boundary.inc')
            code = code.replace(marker, boundary + marker, 1)
            with self.subTest(he=he):
                compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/config/test_wifi_bss_color/main.inc')
