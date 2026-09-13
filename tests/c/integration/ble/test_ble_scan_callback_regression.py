"""Execute production scanner submission/cleanup with controlled SDK callbacks."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import sys
import unittest

from tests.support.c_source import extract as function
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
BLE = ROOT / 'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'

FIXTURE = fixture_text('ble/test_ble_scan_callback_regression/fixture.inc')
MAIN = fixture_text('ble/test_ble_scan_callback_regression/main.inc')

class BleScanCallbackRegression(unittest.TestCase):
    def production(self):
        source = BLE.read_text()
        return '\n'.join(function(source, name) for name in (
            'ble_scan_callback_bind', 'ble_scan_event_callback', 'ble_scan_callbacks_quiesce',
            'ble_release_scanner', 'ble_scan_start'))

    def test_close_waits_for_entered_callback_before_releasing_storage(self):
        import shutil
        import subprocess
        import tempfile
        main = fixture_text('ble/test_ble_scan_callback_regression/test_close_waits_for_entered_callback_before_releasing_storage-main.inc')
        with tempfile.TemporaryDirectory() as temporary:
            source = pathlib.Path(temporary) / 'race.c'
            source.write_text('#define THREADED\n' + FIXTURE + self.production() + main)
            result = subprocess.run([shutil.which('cc') or 'cc', '-std=c11', '-pthread',
                                     str(source), '-o', temporary+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([temporary+'/test'], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_cookie_exhaustion_fails_without_driver_call_or_reuse(self):
        compile_run(self, FIXTURE + self.production() + fixture_text('ble/test_ble_scan_callback_regression/test_cookie_exhaustion_fails_without_driver_call_or_reuse.inc'))

    def test_failed_submission_retires_cookie_and_allows_next_scan(self):
        compile_run(self, FIXTURE + self.production() + fixture_text('ble/test_ble_scan_callback_regression/test_failed_submission_retires_cookie_and_allows_next_scan.inc'))

    def test_completion_during_submission_is_not_resurrected(self):
        self.run_schedule(1)

    def test_callback_copied_before_cancel_cannot_touch_reopened_scan(self):
        self.run_schedule(0)

    def run_schedule(self, immediate):
        compile_run(self, FIXTURE + self.production() + MAIN.replace('IMMEDIATE', str(immediate)))
