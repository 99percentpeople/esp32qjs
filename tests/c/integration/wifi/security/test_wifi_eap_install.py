"""Deferred native EAP install transaction; no execution during API wave.

Real profile allocation/ref/wipe and installer bodies. Only SDK driver/setter,
eloop, allocator and task observation boundaries are injected. Separate SDK
control/lifecycle fixtures exercise the production implementations of those
SDK boundaries; this fixture does not claim live supplicant/RF verification.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.c.integration.wifi.security.test_wifi_enterprise_profile import PRELUDE as PROFILE_PRELUDE
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit, INTERNAL
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, CORE, extract


class WiFiEAPInstall(unittest.TestCase):
    def test_production_install_retirement_policy_and_exact_owner(self):
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h')
        for tls in (0, 1):
            code = PROFILE_PRELUDE + PRELUDE + f'\n#define CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT {tls}\n'
            code += unit(sdk)
            for name in ('esp32_mquickjs_wifi_enterprise_profile.h', 'esp32_mquickjs_wifi_eap_sdk.h', 'esp32_mquickjs_wifi_eap_install.h'):
                code += unit(INTERNAL / name)
            code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
            folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
            code += unit(folder / 'esp32_mquickjs_wifi_enterprise_profile.c')
            code += BOUNDARIES
            code += unit(folder / 'esp32_mquickjs_wifi_eap_install.c')
            compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/security/test_wifi_eap_install/prelude.inc')

BOUNDARIES = fixture_text('wifi/security/test_wifi_eap_install/boundaries.inc')

MAIN = fixture_text('wifi/security/test_wifi_eap_install/main.inc')
