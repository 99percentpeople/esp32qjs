"""Deferred exact per-send results over the production native Session stack."""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.tx.test_wifi_raw_tx_session import production_session_code, MAIN as SESSION_MAIN


class WiFiRawTxResults(unittest.TestCase):
    def test_atomic_registration_eviction_completion_unregister_and_stale_tokens(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        # Reuse only allocator/worker driving helpers, not another test scenario.
        code = production_session_code() + SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            source.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_results/main.inc')
