"""Deferred exact-owner production connection controls and runtime handoff.

Only native state storage, SDK and locks are injected. Does not simulate or prove
RF disconnect/deauth, event delivery/rearming, or physical recovery.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def control_code(profile, ap=True, sdk_extra=()):
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    ap_source = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
    code = '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n'
    code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    code += f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(ap)}\n' + sdk_types(profile, sdk_extra)
    for name in ('esp32_mquickjs_wifi_radio_client_t', 'esp32_mquickjs_wifi_radio_driver_state_t',
                 'esp32_mquickjs_wifi_connection_control_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += 'typedef int esp_err_t;\n'
    for name in ('esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_lifecycle_t',
                 'esp32_mquickjs_wifi_radio_config_result_t', 'esp32_mquickjs_wifi_rssi_request_t'):
        code += structure(header, name)
    code += BOUNDARIES
    code += 'static esp32_mquickjs_wifi_rssi_request_t s_rssi_request;\n'
    code += re.search(r'static struct \{[^}]*\} s_inactive_history;', radio).group(0)
    code += extract(radio, 'wifi_radio_inactive_history_record')
    for name in ('wifi_radio_lease_valid', 'wifi_radio_record_fault', 'wifi_radio_cleanup_fault',
                 'wifi_radio_connection_owner_locked', 'esp32_mquickjs_wifi_radio_rssi_request_status',
                 'esp32_mquickjs_wifi_radio_connection_control'):
        code += extract(radio, name)
    code += extract(wifi, 'esp32_mquickjs_wifi_connection_reserved_locked')
    code += extract(wifi, 'wifi_helpers_idle') + extract(ap_source, 'esp32_mquickjs_wifi_ap_control_lease')
    code += extract(wifi, 'wifi_driver_helpers_ready')
    code += extract(wifi, 'esp32_mquickjs_wifi_apply_connection_control') + RESET
    return code


class WiFiConnectionControls(unittest.TestCase):
    def test_sdk_suffix_failures_exact_identity_handoff_and_rssi_single_write(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, control_code(profile, ap) + MAIN)


BOUNDARIES = fixture_text('wifi/station/test_wifi_connection_controls/boundaries.inc')

RESET = fixture_text('wifi/station/test_wifi_connection_controls/reset.inc')

MAIN = fixture_text('wifi/station/test_wifi_connection_controls/main.inc')
