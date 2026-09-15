"""Deferred production retirement queue scheduling tests; AST only in this wave."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import re
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from sdk_patches.wpa.wps.registrar import patch_source
from sdk_patches.wpa.wps.eapol_retire import RETIRE


class WpsEapolRetire(unittest.TestCase):
    def run_case(self, main):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        relative = 'src/eapol_auth/eapol_auth_sm.c'
        source = patch_source(relative, (Path(sdk) / 'components/wpa_supplicant' / relative).read_bytes()).decode()
        record = re.search(r'struct esp32qjs_wps_eapol_record \{.*?\n\};', source, re.S).group()
        compile_run(self, TYPES + record + HELPERS + RETIRE + BOUNDARIES + main)

    def test_foreign_unlink_publishes_owner_and_native_wake_destroys(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol_retire/test_foreign_unlink_publishes_owner_and_native_wake_destroys.inc'))

    def test_callback_reference_retains_removed_storage_until_outer_dispatch_returns(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol_retire/test_callback_reference_retains_removed_storage_until_outer_dispatch_returns.inc'))

    def test_wake_submission_interleaving_prevents_early_parent_release(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol_retire/test_wake_submission_interleaving_prevents_early_parent_release.inc'))

    def test_wake_oom_retains_queue_explicit_close_drains_and_old_identity_is_inert(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol_retire/test_wake_oom_retains_queue_explicit_close_drains_and_old_identity_is_inert.inc'))


TYPES = fixture_text('wifi/provisioning/wps/test_idf_wps_eapol_retire/types.inc')
HELPERS = fixture_text('wifi/provisioning/wps/test_idf_wps_eapol_retire/helpers.inc')
BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_eapol_retire/boundaries.inc')
