"""Scan admission with production code and recorded C3/C5 SDK declarations."""
import json
import unittest

from tests.support.fixtures import fixture_text
from tests.support.native_compile import compile_run
from tests.support.paths import ROOT
from tests.support.wireless_vm_fixture import extract


class WiFiScanChannels(unittest.TestCase):
    def run_profile(self, profile, implicit=False):
        symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
        declarations = {key.split('::')[-1]: value['declaration'] for key, value in symbols.items()}
        code = '#include <assert.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n#include <string.h>\n'
        code += '#define CONFIG_SOC_WIFI_SUPPORT_5G ' + str(int(profile.startswith('esp32c5/'))) + '\n'
        code += '#define TEST_IMPLICIT ' + str(int(implicit)) + '\n'
        for name in ('wifi_country_policy_t', 'wifi_country_t', 'wifi_band_mode_t',
                     'wifi_scan_type_t', 'wifi_active_scan_time_t', 'wifi_scan_time_t',
                     'wifi_scan_channel_bitmap_t', 'wifi_scan_config_t'):
            code += declarations[name] + ';\n'
        code += fixture_text('wifi/station/test_wifi_scan_channels/sdk.inc')
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for name in ('esp32_mquickjs_wifi_radio_5ghz_channel_bit',
                     'esp32_mquickjs_wifi_radio_validate_scan_channels'):
            code += extract(source, name)
        code += fixture_text('wifi/station/test_wifi_scan_channels/main.inc')
        compile_run(self, code)

    def test_c5_implicit_country_accepts_numeric_and_bitmap_active_and_passive_scans(self):
        self.run_profile('esp32c5/representative', implicit=True)

    def test_c5_explicit_restrictions_errors_and_operation_ownership(self):
        self.run_profile('esp32c5/representative')

    def test_c3_keeps_unsupported_band_and_country_restrictions(self):
        self.run_profile('esp32c3/representative')
