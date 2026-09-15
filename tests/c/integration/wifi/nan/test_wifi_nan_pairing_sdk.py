"""Deferred production PASN command/allocation/timer regressions; no RF claim."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = TEST_ROOT
MODULE = ROOT / 'components/esp32_mquickjs'


def without_includes(source):
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


def extract(source, name):
    match = re.search(r'^[A-Za-z_][\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\n\{', source, re.M)
    if not match:
        raise AssertionError(name)
    return source[match.start():source.index('\n}\n', match.end()) + 3]


class NanPairingNative(unittest.TestCase):
    def test_checked_production_init_timer_and_cleanup(self):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler unavailable')
        sdk = Path(os.environ.get('IDF_PATH', '/home/zach/esp/esp-idf')) / 'components'
        if not sdk.is_dir():
            self.skipTest('Pinned SDK unavailable')
        spec = importlib.util.spec_from_file_location('pairing_patch', ROOT / 'scripts/sdk_patches/wifi/nan_pairing.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            prepared = patch.prepare(sdk, output / 'prepared')
            native = prepared['esp_nan_supplicant.c']
            code = PREFIX
            code += without_includes((MODULE / 'internal/esp32_mquickjs_wifi_nan_pasn_sdk.h').read_text())
            code += without_includes(prepared['esp_nan_supp_i.h'])
            code += BOUNDARY
            code += without_includes((MODULE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pasn_state.inc').read_text())
            for name in ('nan_pasn_auth_timeout_cb', 'nan_pasn_auth_timeout_cancel', 'nan_pasn_auth_timeout_arm',
                         'nan_set_dev_sae_pin', 'nan_pasn_data_deinit', 'nan_pasn_data_init', 'nan_pasn_auth_initiate'):
                code += extract(native, name)
            code += without_includes((MODULE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pasn_commands.inc').read_text())
            code += MAIN
            source, binary = output / 'case.c', output / 'case'
            source.write_text(code)
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)


PREFIX = fixture_text('wifi/nan/test_wifi_nan_pairing_sdk/prefix.inc')

BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_pairing_sdk/boundary.inc')

MAIN = fixture_text('wifi/nan/test_wifi_nan_pairing_sdk/main.inc')
