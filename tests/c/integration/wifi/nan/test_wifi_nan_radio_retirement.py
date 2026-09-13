"""Production NAN Radio cleanup suffix; fixtures run at Wi-Fi phase validation."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = TEST_ROOT
BASE = ROOT / 'components/esp32_mquickjs'


def function(source, name):
    match = re.search(r'^(?:static )?[\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\n\{', source, re.M)
    if match is None:
        raise AssertionError(name)
    return source[match.start():source.index('\n}\n', match.end()) + 3]


class NanRadioRetirement(unittest.TestCase):
    def test_actual_close_keeps_exclusion_through_delayed_stop_detach_and_observer(self):
        self.compile_case(MAIN)

    def test_actual_service_mutation_token_and_cancel_suffix(self):
        self.compile_case(SERVICE_MAIN)

    def compile_case(self, main):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler unavailable')
        header = (BASE / 'internal/esp32_mquickjs_wifi_nan_radio.h').read_text()
        status = header[header.index('typedef struct {'):header.index('/* Background task')]
        production = (BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_nan_radio.inc').read_text()
        start = production.index('struct wifi_radio_nan {')
        binding = production[start:production.index('\n};', start) + 4]
        source = PREFIX + status + binding + BOUNDARY + SERVICE_BOUNDARY
        source += function(production, 'wifi_radio_nan_exact_locked')
        source += function(production, 'esp32_mquickjs_wifi_radio_nan_close')
        source += function(production, 'esp32_mquickjs_wifi_radio_nan_service_start')
        source += function(production, 'esp32_mquickjs_wifi_radio_nan_service_close')
        source += main
        with tempfile.TemporaryDirectory() as folder:
            path, executable = Path(folder) / 'case.c', Path(folder) / 'case'
            path.write_text(source)
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                str(path), '-o', str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)


PREFIX = fixture_text('wifi/nan/test_wifi_nan_radio_retirement/prefix.inc')

BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_radio_retirement/boundary.inc')

MAIN = fixture_text('wifi/nan/test_wifi_nan_radio_retirement/main.inc')

SERVICE_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_radio_retirement/service_boundary.inc')
SERVICE_MAIN = fixture_text('wifi/nan/test_wifi_nan_radio_retirement/service_main.inc')
