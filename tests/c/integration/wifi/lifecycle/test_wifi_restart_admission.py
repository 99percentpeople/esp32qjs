"""Deferred production stopped restart admission, no SDK replacements needed.

Real stopped admission, registry validation and lifecycle reservation are extracted.
Native state and mutex entry are injected; scheduled entry changes exercise the
check/claim boundary, not the actual FreeRTOS scheduler or hardware STOP proof.
"""
from tests.support.fixtures import fixture_text
import re
import unittest

from tests.c.integration.wifi.config.test_wifi_configuration_selection import HEADER, RADIO
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types, structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiRestartAdmission(unittest.TestCase):
    def test_real_stopped_gate_owner_registry_and_atomic_mode_selection(self):
        header, radio = HEADER.read_text(), RADIO.read_text()
        enums = '#undef ESP32_MQUICKJS_WIFI_RADIO_STOPPED\n'
        enums += ''.join(re.search(r'typedef enum \{[^}]*\} ' + name + r';', header).group(0)
                        for name in ('esp32_mquickjs_wifi_radio_client_t',
                                     'esp32_mquickjs_wifi_radio_driver_state_t',
                                     'esp32_mquickjs_wifi_stop_snapshot_step_t'))
        declarations = ''.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_lifecycle_t',
            'esp32_mquickjs_wifi_radio_stop_snapshot_t', 'esp32_mquickjs_wifi_radio_restart_selection_t'))
        limits = '#undef WIFI_RADIO_MAX_LEASES\n' + re.search(
            r'^#define WIFI_RADIO_MAX_LEASES .*$', radio, re.M).group(0) + '\n'
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_lease_valid', 'wifi_radio_stop_snapshot_unchanged_locked',
            'wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked', 'esp32_mquickjs_wifi_radio_begin_stopped_restart'))
        for profile in ('esp32c3', 'esp32s3', 'esp32c5'):
            for ap in (0, 1):
                with self.subTest(target=profile, softap=ap):
                    code = PRELUDE + limits + f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n'
                    variant = profile + ('/representative-psram' if profile == 'esp32s3' else '/representative')
                    code += sdk_types(variant) + enums + declarations
                    compile_run(self, code + BOUNDARIES + functions + MAIN)


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_restart_admission/boundaries.inc')


MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_admission/main.inc')
