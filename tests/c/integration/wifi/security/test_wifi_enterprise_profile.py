"""Deferred production EAP profile storage, bounds, refs and secure destruction.

Only allocator and critical-section boundaries are injected. No SDK/Radio/EAP
lifecycle or authentication is simulated by this fixture. AST only in API wave.
"""
from tests.support.fixtures import fixture_text
import unittest
from pathlib import Path
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit, INTERNAL
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, CORE, extract


class WiFiEnterpriseProfile(unittest.TestCase):
    def test_production_storage_limits_failure_and_wipe(self):
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h')
        for tls in (0, 1):
            for suiteb in (0, 1):
                code = PRELUDE + f'\n#define CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT {tls}\n#define CONFIG_ESP_WIFI_SUITE_B_192 {suiteb}\n'
                code += unit(sdk)
                code += unit(INTERNAL / 'esp32_mquickjs_wifi_enterprise_profile.h')
                code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
                code += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise/esp32_mquickjs_wifi_enterprise_profile.c')
                compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/security/test_wifi_enterprise_profile/prelude.inc')

MAIN = fixture_text('wifi/security/test_wifi_enterprise_profile/main.inc')
