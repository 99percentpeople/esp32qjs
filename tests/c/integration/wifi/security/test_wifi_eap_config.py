"""Deferred production EAP config owner, revisions, control tokens and close.

Uses actual profile/config implementations; only RTOS/allocator and the Radio
binding observation are boundaries. Getter reentry is the same production
snapshot/commit ordering, without a replacement configuration state machine.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.c.integration.wifi.security.test_wifi_enterprise_profile import PRELUDE as PROFILE_PRELUDE
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit, INTERNAL
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, CORE, extract


class WiFiEAPConfig(unittest.TestCase):
    def test_stale_commit_busy_pin_exact_finish_and_runtime_retirement(self):
        code = PROFILE_PRELUDE.replace('static bool critical,allocation_fail;', 'static unsigned critical;static bool allocation_fail;')
        code = code.replace('assert(!critical);critical=true;', 'assert(critical<2);critical++;')
        code = code.replace('assert(critical);critical=false;', 'assert(critical);critical--;')
        code += '\n#define CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT 1\n#define ESP_ERR_INVALID_STATE 5\n'
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h')
        code += unit(sdk)
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_enterprise_profile.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_config.h')
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
        code += unit(folder / 'esp32_mquickjs_wifi_enterprise_profile.c')
        code += BOUNDARY + unit(folder / 'esp32_mquickjs_wifi_eap_config.c')
        compile_run(self, code + MAIN)


BOUNDARY = fixture_text('wifi/security/test_wifi_eap_config/boundary.inc')
MAIN = fixture_text('wifi/security/test_wifi_eap_config/main.inc')
