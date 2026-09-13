"""Execute the device regression itself against the native API's lazy-tree shape.

Node only supplies the deterministic boundary fixture here; MQuickJS check-js
remains the syntax authority for the unchanged ES5 device source.
"""
from tests.support.paths import ROOT as TEST_ROOT
import json
import pathlib
import shutil
import subprocess
import unittest

ROOT = TEST_ROOT
SOURCE = ROOT / 'tests/js/flash_data/modules/wireless_core/lifecycle-existing-api.js'


class WirelessMemorySampling(unittest.TestCase):
    def run_fixture(self, leak, cleanup_pending=False):
        node = shutil.which('node')
        self.assertIsNotNone(node, 'Node is required for the lazy getter boundary fixture')
        fixture = (ROOT / 'tests/js/host-fixtures/memory-sampling.js').read_text().replace('LEAK', 'true' if leak else 'false').replace(
            'CLEANUP_PENDING', 'true' if cleanup_pending else 'false')
        harness = (ROOT / 'tests/js/flash_data/_test/harness.js').read_text()
        start = harness.index('  helper.memorySnapshot = function () {')
        end = harness.index('\n  };', start) + len('\n  };')
        sampler = harness[start:end].replace('helper.memorySnapshot', 'test.memorySnapshot')
        result = subprocess.run([node, '-e', fixture+sampler+SOURCE.read_text()], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_samples_are_captured_at_the_two_measurement_points(self):
        value = self.run_fixture(False)['value']
        self.assertEqual(value['before']['internal']['freeBytes'], 99984)
        self.assertEqual(value['after']['internal']['freeBytes'], 99904)

    def test_managed_owner_leak_is_detected_despite_lazy_getters(self):
        self.assertEqual(self.run_fixture(True).get('error'), 'managed PSRAM payload imbalance')

    def test_pending_native_task_cleanup_is_not_reported_as_a_settled_sample(self):
        self.assertEqual(self.run_fixture(False, cleanup_pending=True).get('error'),
                         'native task cleanup did not settle within 1000 ms')
