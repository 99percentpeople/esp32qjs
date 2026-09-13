"""Deferred tests of the production periodic ledger (no replacement scheduler).

Timer/worker, Session admission and SDK ownership integration are separate gates.
Do not import/compile/run this fixture until the Wi-Fi stage test phase.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = TEST_ROOT
INTERNAL = ROOT / 'components/esp32_mquickjs/internal'
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_periodic.c'


class WiFiRawTxPeriodic(unittest.TestCase):
    def test_deadlines_backpressure_exact_tickets_and_shutdown(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / 'fixture.c', Path(directory) / 'fixture'
            source.write_text(MAIN)
            built = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(INTERNAL), str(SOURCE), str(source), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_periodic/main.inc')
