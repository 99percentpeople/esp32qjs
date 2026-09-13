"""Deferred production arbiter tests; only task-lock boundaries are substituted."""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, unit


class WiFiRawTxLane(unittest.TestCase):
    def test_fifo_exact_identity_cancellation_capacity_exhaustion_and_concurrency(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        code = PRELUDE
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_lane.h')
        code += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_lane.c')
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            source.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


PRELUDE = fixture_text('wifi/tx/test_wifi_raw_tx_lane/prelude.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_lane/main.inc')
