"""Deferred production stop/AP handoff and cleanup suffix, with SDK/config boundaries.

Exact Radio binding/lifecycle proof is in test_wifi_eap_radio. Config/profile
retention has its own production fixture; this fixture exercises the real helper
coordinator including retries after AP coordinator storage has retired.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.config.test_wifi_configuration_cleanup import BOUNDARIES, WIFI, AP
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiEnterpriseStop(unittest.TestCase):
    def test_admission_clear_failure_and_completed_suffix_not_repeated(self):
        wifi, ap = WIFI.read_text(), AP.read_text()
        code = BOUNDARIES
        code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1\n#define WIFI_MODE_STA 1\n#define WIFI_MODE_APSTA 3\n'
        code += 'typedef struct esp32_mquickjs_wifi_eap_profile esp32_mquickjs_wifi_eap_profile_t;\n'
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_config.h')
        code += EAP_BOUNDARIES
        for name in ['wifi_ap_retire_netif','wifi_ap_retire_for_token',
                     'esp32_mquickjs_wifi_ap_begin_enterprise_stop',
                     'esp32_mquickjs_wifi_ap_begin_configuration',
                     'esp32_mquickjs_wifi_ap_release_enterprise_stop',
                     'esp32_mquickjs_wifi_ap_retire_for_configuration',
                     'esp32_mquickjs_wifi_ap_retire_for_recovery']:
            code += extract(ap, name)
        for name in ['esp32_mquickjs_wifi_connection_reserved_locked','wifi_helpers_idle','esp32_mquickjs_wifi_retire_for_recovery',
                     'wifi_begin_configuration_cleanup','wifi_begin_enterprise_stop',
                     'wifi_finish_enterprise_clear','wifi_finish_configuration_cleanup','wifi_stop_idle',
                     'esp32_mquickjs_wifi_eap_prepare_runtime_destroy']:
            code += extract(wifi, name)
        compile_run(self, code + MAIN)


EAP_BOUNDARIES = fixture_text('wifi/security/test_wifi_enterprise_stop/eap_boundaries.inc')
MAIN = fixture_text('wifi/security/test_wifi_enterprise_stop/main.inc')
