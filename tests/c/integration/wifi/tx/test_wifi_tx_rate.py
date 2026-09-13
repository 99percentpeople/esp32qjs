"""Deferred production TX rate ledger and Radio admission tests.

Compiles exact SDK inventory types plus production helpers/wrappers. The fixture
injects only driver calls and locks/state storage. Run in the Wi-Fi stage phase;
this implementation batch performs AST parsing only.
"""
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
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
COMPONENT = ROOT / 'components/esp32_mquickjs'


def rate_code(profile, predefined=()):
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {}
    for key, entry in symbols.items():
        declaration = entry.get('declaration', '')
        if declaration.startswith('typedef '):
            name = key.split('::')[-1]
            if name not in declarations or '{' in declaration:
                declarations[name] = declaration
    seen, output = set(predefined), []

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        for dependency in sorted(set(re.findall(r'\b\w+_t\b', declarations[name])) - {name}):
            if dependency in declarations:
                visit(dependency)
        output.append(declarations[name] + ';\n')

    for name in ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_phy_mode_t', 'wifi_tx_rate_config_t'):
        visit(name)
    code = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n#define CONFIG_SOC_WIFI_SUPPORTED 1\n'
    if profile.startswith('esp32c5/'):
        code += '#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_SOC_WIFI_SUPPORT_5G 1\n'
    code += '#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n#include <assert.h>\n'
    code += 'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_NOT_SUPPORTED 5\n'
    code += ''.join(output)
    for path in ('internal/esp32_mquickjs_wifi_tx_rate.h',
                 'src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate.c'):
        code += re.sub(r'^#(?:pragma once|include .*).*$', '', (COMPONENT / path).read_text(), flags=re.M)
    return code


class WiFiTxRate(unittest.TestCase):
    def test_records_rollback_fault_admission_repair_and_identity_exhaustion(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = rate_code(profile) + BOUNDARIES
                for name in ('wifi_radio_record_fault', 'wifi_radio_cleanup_fault',
                             'wifi_radio_write_tx_rate', 'esp32_mquickjs_wifi_radio_tx_rate_status',
                             'esp32_mquickjs_wifi_radio_configure_tx_rate'):
                    code += extract(radio, name)
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_real_restart_capture_replay_suffix_and_identity_budget(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, rate_code(profile) + REPLAY_MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_tx_rate/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_tx_rate/main.inc')


REPLAY_MAIN = fixture_text('wifi/tx/test_wifi_tx_rate/replay_main.inc')
