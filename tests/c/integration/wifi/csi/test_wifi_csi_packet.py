"""Deferred CSI packet tests against production receipts, parser, pool and wire.

The implementation wave only AST-parses this file. Execution belongs to the
Wi-Fi phase gate; no separate packet/lifecycle model is used as an oracle.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = TEST_ROOT
BASE = ROOT / 'components/esp32_mquickjs'


class WiFiCsiPacket(unittest.TestCase):
    def test_proven_span_required_truncation_ownership_budget_and_wire(self):
        sources = [BASE / 'src' / p for p in (
            'core/esp32_mquickjs_native_pool.c', 'core/esp32_mquickjs_native_lease.c',
            'modules/wifi_csi/esp32_mquickjs_wifi_csi_rx_span.c',
            'modules/wifi_csi/esp32_mquickjs_wifi_csi_packet.c',
            'modules/wifi_csi/esp32_mquickjs_wifi_csi_resources.c',
            'modules/wifi_csi/esp32_mquickjs_wifi_csi_store.c',
            'modules/wifi_csi/esp32_mquickjs_wifi_csi_wire.c',
            'modules/wifi_common/esp32_mquickjs_wifi_rx.c',
            'modules/wifi_common/esp32_mquickjs_wifi_rx_wire.c',
            'modules/wifi_common/esp32_mquickjs_wifi_rx_wire_metadata.c')]
        with tempfile.TemporaryDirectory() as directory:
            code = Path(directory) / 'packet.c'
            binary = Path(directory) / 'packet'
            code.write_text(CODE)
            result = subprocess.run(['cc', '-std=c11', '-D_GNU_SOURCE', '-Wall', '-Wextra', '-Werror',
                '-I' + str(BASE / 'internal'), str(code), *map(str, sources), '-o', str(binary)],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


CODE = fixture_text('wifi/csi/test_wifi_csi_packet/code.inc')
