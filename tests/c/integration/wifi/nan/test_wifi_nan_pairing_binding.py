"""Deferred tests of production pairing admission and selected NPK lookup.

Native service/cache storage and crypto submission are injected boundaries;
the actual binding, command dispatch and credential-selection helpers run.
These fixtures are authored now, executed with the Wi-Fi stage validation.
"""
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
BASE = ROOT / 'components/esp32_mquickjs'


def clean(source):
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


def extract(source, name):
    match = re.search(r'^[A-Za-z_][\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\n\{', source, re.M)
    if not match:
        raise AssertionError(name)
    return source[match.start():source.index('\n}\n', match.end()) + 3]


class PairingBinding(unittest.TestCase):
    def test_exact_service_explicit_credential_failure_and_scrub(self):
        compiler = shutil.which('cc')
        sdk = Path(os.environ.get('IDF_PATH', '/home/zach/esp/esp-idf')) / 'components'
        if not compiler or not sdk.is_dir():
            self.skipTest('Host compiler and pinned SDK required')
        spec = importlib.util.spec_from_file_location('nan_pairing_binding_patch', ROOT / 'scripts/sdk_patches/wifi/nan_pairing.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            native = patch.prepare(sdk, folder / 'prepared')['nan_pairing.c']
            spec = importlib.util.spec_from_file_location('nan_cache_patch', ROOT / 'scripts/sdk_patches/wifi/nan.py')
            cache_patch = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cache_patch)
            app = cache_patch.patch_source((sdk / 'esp_wifi/wifi_apps/nan_app/src/nan_app.c').read_text())
            code = PREFIX + clean((BASE / 'internal/esp32_mquickjs_wifi_nan_pasn_sdk.h').read_text()) + BOUNDARY
            code += clean((BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_binding.inc').read_text())
            code += extract(native, 'nan_peer_cred_npk_present')
            code += extract(native, 'nan_peer_cred_lookup')
            code += extract(native, 'nan_global_peer_npk_lookup')
            sdk_bridge = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
            for name in ('esp32qjs_nan_pairing_reset_service', 'esp32_mquickjs_wifi_nan_pairing_cached_peer'):
                code += extract(sdk_bridge, name)
            for name in ('nan_app_find_paired_slot_locked', 'nan_app_alloc_paired_slot_locked', 'nan_app_register_paired_peer',
                         'nan_app_remove_paired_peer'):
                code += extract(app, name)
            code += NATIVE
            code += clean((BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_start.inc').read_text())
            code += extract(native, 'nan_app_update_peer_creds')
            code += clean((BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_cache.inc').read_text()) + MAIN
            source, binary = folder / 'case.c', folder / 'case'
            source.write_text(code)
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)


PREFIX = fixture_text('wifi/nan/test_wifi_nan_pairing_binding/prefix.inc')

BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_pairing_binding/boundary.inc')

NATIVE = fixture_text('wifi/nan/test_wifi_nan_pairing_binding/native.inc')

MAIN = fixture_text('wifi/nan/test_wifi_nan_pairing_binding/main.inc')
