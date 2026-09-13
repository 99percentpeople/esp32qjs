"""Deferred production SmartConfig ACK regressions; execute in Wi-Fi phase tests.

Compiles the complete patched SDK translation unit with controllable task,
allocator, socket and event boundaries. No independent lifecycle model.
"""
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
from patch_idf_smartconfig import patch_source, patch_adapter, patch_decoder_null_stores, function


class SmartConfigNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.environ.get('IDF_PATH'):
            raise unittest.SkipTest('IDF_PATH must name the reviewed SDK')
        cls.sdk = Path(os.environ['IDF_PATH']) / 'components/esp_wifi'

    def test_drift_and_double_patch_rejected(self):
        for name in ('src/smartconfig.c', 'src/smartconfig_ack.c'):
            source = (self.sdk / name).read_bytes()
            for changed in (source + b'\n', patch_source(name, source)):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    patch_source(name, changed)

    def test_no_credential_log_or_temporary_copy(self):
        name = 'src/smartconfig.c'
        original = (self.sdk / name).read_bytes()
        self.assertIn(b'"PASSWORD:%s"', original)
        patched = patch_source(name, original)
        self.assertNotIn(b'evt->password', patched)
        self.assertNotIn(b'evt->ssid', patched)
        self.assertNotIn(b'"PASSWORD:%s"', patched)

    def test_production_ack_task_and_control(self):
        name = 'src/smartconfig_ack.c'
        source = patch_source(name, (self.sdk / name).read_bytes()).decode()
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
        compile_run(self, BOUNDARIES + source + CASES)

    def test_decoder_adapter_preserves_shared_table_and_rejects_drift(self):
        for target in ('esp32c3', 'esp32s3', 'esp32c5'):
            original = (self.sdk / target / 'esp_adapter.c').read_bytes()
            patched = patch_adapter(target, original)
            self.assertTrue(patched.startswith(original))
            self.assertEqual(patched.count(b'._free = free,'), 1)
            self.assertEqual(patched.count(b'._free = esp32qjs_smartconfig_secure_free,'), 1)
            self.assertEqual(patched.count(b'._wifi_calloc = wifi_calloc,'), 1)
            self.assertEqual(patched.count(b'._wifi_calloc = esp32qjs_smartconfig_calloc,'), 1)
            self.assertIn(b'static const wifi_osi_funcs_t s_esp32qjs_smartconfig_osi', patched)
            self.assertIn(b'const wifi_osi_funcs_t *const esp32qjs_smartconfig_osi', patched)
            for invalid in (original + b'\n', patched):
                with self.assertRaises(ValueError):
                    patch_adapter(target, invalid)

    def test_decoder_full_allocation_erased_before_free(self):
        # Production body from the generated adapter, not a second implementation.
        patched = patch_adapter('esp32c3', (self.sdk / 'esp32c3/esp_adapter.c').read_bytes()).decode()
        body = function(patched, 'esp32qjs_smartconfig_secure_free')
        compile_run(self, SECRET_FREE_BOUNDARIES + body + SECRET_FREE_CASES)

    def test_reviewed_decoder_null_stores_only_and_drift_rejection(self):
        # This calls the build's production transformer on real SDK archives.
        # It is structural evidence; target OOM execution remains a separate gate.
        for target in ('esp32c3', 'esp32s3', 'esp32c5'):
            original = (self.sdk / 'lib' / target / 'libsmartconfig.a').read_bytes()
            patched = patch_decoder_null_stores(target, original)
            self.assertEqual(len(original), len(patched))
            differences = [i for i, (a, b) in enumerate(zip(original, patched)) if a != b]
            self.assertTrue(differences)
            self.assertLessEqual(len(differences), 8)
            for invalid in (original + b'\n', patched):
                with self.assertRaises(ValueError):
                    patch_decoder_null_stores(target, invalid)

    def test_production_allocator_reports_only_nonempty_failure(self):
        patched = patch_adapter('esp32c3', (self.sdk / 'esp32c3/esp_adapter.c').read_bytes()).decode()
        body = function(patched, 'esp32qjs_smartconfig_calloc')
        compile_run(self, OBSERVED_CALLOC_BOUNDARIES + body + OBSERVED_CALLOC_CASES)


OBSERVED_CALLOC_BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_idf_smartconfig/observed_calloc_boundaries.inc')

OBSERVED_CALLOC_CASES = fixture_text('wifi/provisioning/smartconfig/test_idf_smartconfig/observed_calloc_cases.inc')


SECRET_FREE_BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_idf_smartconfig/secret_free_boundaries.inc')

SECRET_FREE_CASES = fixture_text('wifi/provisioning/smartconfig/test_idf_smartconfig/secret_free_cases.inc')

BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_idf_smartconfig/boundaries.inc')

CASES = fixture_text('wifi/provisioning/smartconfig/test_idf_smartconfig/cases.inc')
