"""Deferred actual SDK DPP selection and independent-introduction timeout cases."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT


class DppConnectionNative(unittest.TestCase):
    def test_exact_selection_and_timeout_without_old_provisioning_auth(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        sys.path.insert(0, str(ROOT / 'scripts'))
        from sdk_patches.wpa.dpp import patch_source, function
        original = Path(sdk) / 'components/wpa_supplicant/esp_supplicant/src/esp_dpp.c'
        source = patch_source('esp_supplicant/src/esp_dpp.c', original.read_bytes()).decode()
        code = BOUNDARIES
        for name in ('esp32qjs_dpp_connection_locked', 'esp32qjs_dpp_native_select',
                     'esp32qjs_dpp_native_check_connection', 'peer_disc_timeout'):
            code += function(source, name)
        compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_dpp_connection_native/boundaries.inc')
MAIN = fixture_text('wifi/provisioning/dpp/test_dpp_connection_native/main.inc')
