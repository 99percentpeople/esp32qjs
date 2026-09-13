"""Deferred production scan transaction, SDK excess-copy boundary and restart intent.

Only SDK storage/failure, locks and physical activation are injected. These do
not prove actual RF scan timing, SDK normalization or device lifecycle behavior.
"""
from tests.support.fixtures import fixture_text
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def scan_support(radio):
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_scan_parameters.h').read_text()
    header = re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M)
    code = BOUNDARIES + header
    code += re.search(r'static struct \{[^}]*\} s_scan_parameters;', radio).group(0)
    code += extract(radio, 'wifi_radio_scan_parameters_observe_locked')
    code += extract(radio, 'wifi_radio_restore_scan_parameters_locked')
    return code


def scan_code(profile, ap=True):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    code = control_code(profile, ap, ('wifi_scan_default_params_t',)) + scan_support(radio)
    code += 'static struct {bool unchanged;} s_stop_snapshot;\n'
    code += extract(radio, 'wifi_radio_invalidate_stop_snapshot_locked')
    for name in ('esp32_mquickjs_wifi_radio_read_scan_parameters', 'esp32_mquickjs_wifi_radio_write_scan_parameters'):
        code += extract(radio, name)
    code += extract(wifi, 'esp32_mquickjs_wifi_apply_scan_parameters')
    return code


class WiFiScanParameters(unittest.TestCase):
    def test_production_sdk_failures_exact_owners_padding_and_activation(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, scan_code(profile) + MAIN)


    def test_unadapted_sdk_copy_fails_asan_and_production_padding_survives(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        production = scan_code('esp32c5/representative')
        unadapted = production.replace(extract(production, 'wifi_scan_parameters_set_native'),
            'static int wifi_scan_parameters_set_native(const wifi_scan_default_params_t *p) { return esp_wifi_set_scan_parameters(p); }\n')
        main = fixture_text('wifi/station/test_wifi_scan_parameters/test_unadapted_sdk_copy_fails_asan_and_production_padding_survives-main.inc')
        with tempfile.TemporaryDirectory() as tmp:
            for adapted, body in ((False, unadapted), (True, production)):
                source = Path(tmp) / ('adapted.c' if adapted else 'unadapted.c')
                binary = source.with_suffix('')
                source.write_text(body + main)
                # Force the full native-sized read through ASAN's memcpy interceptor;
                # GCC's inline copy can miss the poisoned middle of this span.
                compiled = subprocess.run([compiler, '-std=c11', '-g', '-fsanitize=address',
                    '-fno-omit-frame-pointer', '-fno-builtin-memcpy', str(source), '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                executed = subprocess.run([str(binary)], capture_output=True, text=True)
                if adapted:
                    self.assertEqual(executed.returncode, 0, executed.stderr)
                else:
                    self.assertNotEqual(executed.returncode, 0)
                    self.assertIn('stack-buffer-overflow', executed.stderr)


BOUNDARIES = fixture_text('wifi/station/test_wifi_scan_parameters/boundaries.inc')

MAIN = fixture_text('wifi/station/test_wifi_scan_parameters/main.inc')
