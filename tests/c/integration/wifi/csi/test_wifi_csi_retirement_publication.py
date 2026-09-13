"""Deferred production CSI store/resource tests; no alternate lifecycle model."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = TEST_ROOT


class WiFiCsiRetirementPublication(unittest.TestCase):
    def test_generations_budget_failure_identity_and_allocator_return_barrier(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        base = ROOT / 'components/esp32_mquickjs'
        sources = [base / 'src/core' / ('esp32_mquickjs_' + name + '.c')
                   for name in ('native_pool', 'native_lease')]
        sources += [base / 'src/modules/wifi_csi' / ('esp32_mquickjs_wifi_csi_' + name + '.c')
                    for name in ('resources', 'store', 'packet')]
        sources.append(base / 'src/modules/wifi_common/esp32_mquickjs_wifi_rx.c')
        with tempfile.TemporaryDirectory() as temp:
            source, binary = pathlib.Path(temp) / 'fixture.c', pathlib.Path(temp) / 'fixture'
            source.write_text(CODE)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    '-I' + str(base / 'internal'), str(source), *map(str, sources),
                                    '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)


CODE = fixture_text('wifi/csi/test_wifi_csi_retirement_publication/code.inc')
