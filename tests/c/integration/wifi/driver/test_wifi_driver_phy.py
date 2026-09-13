"""Deferred production Radio PHY readback tests; implementation batch: AST only."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.support.wireless_vm_fixture import extract

ROOT = TEST_ROOT
COMPONENT = ROOT / 'components/esp32_mquickjs'


def phy_types(profile, ap=True):
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {key.split('::')[-1]: item['declaration'] for key, item in symbols.items()}
    code = '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n'
    code += f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(ap)}\n'
    code += f'#define CONFIG_SOC_WIFI_SUPPORT_5G {int(profile.startswith("esp32c5/"))}\n'
    for name in ('wifi_interface_t', 'wifi_mode_t', 'wifi_band_mode_t', 'wifi_bandwidth_t', 'wifi_bandwidths_t', 'wifi_protocols_t'):
        code += declarations[name] + ';\n'
    for name in ('11B', '11G', '11N', '11A', '11AC', '11AX', 'LR'):
        code += '#define ' + declarations['WIFI_PROTOCOL_' + name] + '\n'
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    for kind, name in (('enum', 'esp32_mquickjs_wifi_phy_query_t'),
                       ('struct', 'esp32_mquickjs_wifi_phy_readback_t'),
                       ('enum', 'esp32_mquickjs_wifi_radio_driver_state_t')):
        code += re.search(r'typedef ' + kind + r' \{[^}]*\} ' + name + ';', header).group(0) + '\n'
    return code


class WiFiDriverPhy(unittest.TestCase):
    def test_sdk_read_errors_admission_inactive_bands_and_no_partial_output(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap), tempfile.TemporaryDirectory() as tmp:
                    path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                    path.write_text(phy_types(profile, ap) + BOUNDARIES +
                                    extract(source, 'esp32_mquickjs_wifi_radio_read_phy') + MAIN)
                    result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                             str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_phy/boundaries.inc')

MAIN = fixture_text('wifi/driver/test_wifi_driver_phy/main.inc')
