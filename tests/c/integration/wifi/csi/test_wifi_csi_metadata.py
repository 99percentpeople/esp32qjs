"""Deferred production CSI target normalizers with explicit SDK input boundaries.

The SDK-shaped input intentionally omits mac/dmac/rx_seq: publication must get
those facts from a proven header, never the conditionally initialized SDK fields.
The real target normalizer, signal decoder and CSI layout implementation run here.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import subprocess
import tempfile
import unittest
from tests.support.wireless_vm_fixture import extract

ROOT = TEST_ROOT
BASE = ROOT / 'components/esp32_mquickjs'
CSI = BASE / 'src/modules/wifi_csi'


class WiFiCsiMetadata(unittest.TestCase):
    def test_target_availability_reserved_formats_and_header_facts(self):
        for he in (False, True):
            with self.subTest(he=he), tempfile.TemporaryDirectory() as directory:
                source = (CSI / ('esp32_mquickjs_wifi_csi_target_he.c' if he else
                                 'esp32_mquickjs_wifi_csi_target_legacy.c')).read_text()
                helpers = ('he_phy', 'he_secondary') if he else ('legacy_secondary',)
                code = BOUNDARIES + '\n'.join(extract(source, name) for name in
                    (*helpers, 'esp32_mquickjs_wifi_csi_target_normalize_metadata'))
                code += ('#define TEST_HE 1\n' if he else '#define TEST_HE 0\n') + MAIN
                path = Path(directory) / 'metadata.c'; path.write_text(code)
                binary = Path(directory) / 'metadata'
                sources = [CSI / name for name in ('esp32_mquickjs_wifi_csi_target_config.c',
                                                   'esp32_mquickjs_wifi_csi_target_layout.c')]
                result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(BASE / 'internal'), str(path), *map(str, sources), '-o', str(binary)],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = fixture_text('wifi/csi/test_wifi_csi_metadata/boundaries.inc')
MAIN = fixture_text('wifi/csi/test_wifi_csi_metadata/main.inc')
