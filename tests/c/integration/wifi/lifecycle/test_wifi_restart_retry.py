"""Deferred production retry admission and immutable checkpoint identity.

Native driver state, feature admission and lock-entry changes are injected.
The exact checkpoint predicate and retry admission are production code. No
SDK calls exist in this fixture; runtime phase ordering is covered separately.
"""
from tests.support.fixtures import fixture_text
import re
import unittest

from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, HEADER, RADIO, sdk_types, structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiRestartRetry(unittest.TestCase):
    def test_complete_checkpoint_fault_ownership_capacity_and_off_consent(self):
        header, radio = HEADER.read_text(), RADIO.read_text()
        declarations = '#undef ESP32_MQUICKJS_WIFI_RADIO_STOPPED\n'
        declarations += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_driver_state_t;', header).group(0)
        declarations += ''.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lifecycle_t', 'esp32_mquickjs_wifi_radio_restart_selection_t',
            'esp32_mquickjs_wifi_radio_config_result_t'))
        limits = '#undef WIFI_RADIO_MAX_LEASES\n' + re.search(r'^#define WIFI_RADIO_MAX_LEASES .*$', radio, re.M).group(0) + '\n'
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_restart_checkpoint_matches_locked', 'esp32_mquickjs_wifi_radio_admit_restart_retry'))
        for profile in ('esp32c3', 'esp32s3', 'esp32c5'):
            for ap in (0, 1):
                with self.subTest(target=profile, softap=ap):
                    code = PRELUDE + limits + f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n'
                    code += f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(profile=="esp32c5")}\n'
                    code += f'#define CONFIG_IDF_TARGET_ESP32C5 {int(profile=="esp32c5")}\n'
                    variant = profile + ('/representative-psram' if profile=='esp32s3' else '/representative')
                    compile_run(self, code + sdk_types(variant) + declarations + BOUNDARIES + functions + MAIN)


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_restart_retry/boundaries.inc')


MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_retry/main.inc')
