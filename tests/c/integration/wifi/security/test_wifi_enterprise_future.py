"""Deferred production Enterprise worker/poll/cancel and config/profile lifetime.

No alternative state machine: uses actual driver helpers plus config/profile.
SDK Radio calls and background queue are controlled boundaries. Full Future core
scheduling and real VM result conversion remain separate stage requirements.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.c.integration.wifi.security.test_wifi_enterprise_profile import PRELUDE
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit, INTERNAL
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, CORE, extract


class WiFiEnterpriseFuture(unittest.TestCase):
    def test_queue_cancel_late_sdk_return_and_cleanup_retry(self):
        code = PRELUDE.replace('static bool critical,allocation_fail;', 'static unsigned critical;static bool allocation_fail;')
        code = code.replace('assert(!critical);critical=true;', 'assert(critical<2);critical++;')
        code = code.replace('assert(critical);critical=false;', 'assert(critical);critical--;')
        code += '\n#include <stdatomic.h>\n#define ESP_ERR_INVALID_STATE 5\n#define CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT 1\n'
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h')
        code += unit(sdk) + unit(INTERNAL / 'esp32_mquickjs_wifi_enterprise_profile.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_config.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_sdk.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_install.h')
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
        code += unit(folder / 'esp32_mquickjs_wifi_enterprise_profile.c')
        code += 'static uint64_t binding;\nstatic uint64_t esp32_mquickjs_wifi_radio_eap_identity(void){assert(!critical);return binding;}\n'
        code += unit(folder / 'esp32_mquickjs_wifi_eap_config.c')
        code += TYPES
        source = (folder / 'esp32_mquickjs_wifi_enterprise.c').read_text()
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += BOUNDARIES + "\n#define heap_caps_free free\n"
        for name in ['enterprise_worker', 'enterprise_start', 'enterprise_poll',
                     'enterprise_cancel', 'enterprise_destroy', 'enterprise_timeout']:
            code += extract(source, name)
        compile_run(self, code + MAIN)


TYPES = fixture_text('wifi/security/test_wifi_enterprise_future/types.inc')
BOUNDARIES = fixture_text('wifi/security/test_wifi_enterprise_future/boundaries.inc')
MAIN = fixture_text('wifi/security/test_wifi_enterprise_future/main.inc')
