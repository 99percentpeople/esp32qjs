"""SDK preflight must inspect all inputs and compose actual predecessor outputs."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

from tests.support.paths import ROOT
from tests.support.idf import require_idf

sys.path.insert(0, str(ROOT / 'scripts'))
from sdk_patches.check import check_composition, check_inputs
from sdk_patches.registry import catalog, reviewed_inputs, render_cmake
from sdk_patches.common.source import function, replace


class SdkPatchRegistry(unittest.TestCase):
    def test_preflight_reports_every_missing_and_changed_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'changed').write_bytes(b'new')
            (root / 'matched').write_bytes(b'old')
            inputs = {name: {'sha256': hashlib.sha256(b'old').hexdigest(), 'patches': ['example']}
                      for name in ('missing', 'changed', 'matched')}
            rows = check_inputs(root, inputs)
            self.assertEqual([row['status'] for row in rows], ['missing', 'changed', 'matched'])
            self.assertEqual((root / 'changed').read_bytes(), b'new')

    def test_order_and_cmake_fingerprints_have_one_registry(self):
        generated = render_cmake()
        expected = [patch['cmake'] for patch in catalog()['patches'] if patch['apply']]
        positions = [generated.index('/' + filename + '"') for filename in expected]
        self.assertEqual(positions, sorted(positions))
        self.assertLess(generated.index('/patch_idf_raw_tx_management.cmake'),
                        generated.index('/patch_idf_raw_tx_identity.cmake'))
        self.assertTrue(all(record['patches'] for record in reviewed_inputs().values()))

    def test_complete_archive_composition_in_supported_feature_variants(self):
        sdk = require_idf()
        for target in ('esp32c3', 'esp32s3', 'esp32c5'):
            for extended in (False, True):
                with self.subTest(target=target, extended=extended):
                    result = check_composition(sdk, target, extended)
                    self.assertEqual(len(result['net80211Sha256']), 64)

    def test_source_transform_rejects_missing_and_ambiguous_definitions(self):
        source = 'static int selected(void)\n{\n    return 1;\n}\n'
        self.assertEqual(function(source, 'selected'), source)
        self.assertEqual(function(source, 'selected', lambda s: replace(s, 'return 1', 'return 2')),
                         source.replace('return 1', 'return 2'))
        for invalid in ('', source + source):
            with self.assertRaises(ValueError):
                function(invalid, 'selected')
        with self.assertRaises(ValueError):
            replace('same same', 'same', 'other')
