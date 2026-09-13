"""Run production BLE callback bodies with a deterministic native fixture.

SDK submission notifications and ATT confirmations are separate events. The
fixture replaces SDK/RTOS boundaries, not the production completion branch.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = TEST_ROOT
BLE = ROOT / 'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'


class BleControlRegression(unittest.TestCase):
    def test_indication_submission_is_not_confirmation(self):
        source = BLE.read_text()
        start = source.index('    case BLE_GAP_EVENT_NOTIFY_TX: {')
        end = source.index('    case BLE_GAP_EVENT_SUBSCRIBE:', start)
        branch = source[start:end]
        fixture = fixture_text('ble/test_ble_control_regression/test_indication_submission_is_not_confirmation-fixture.inc')
        main = fixture_text('ble/test_ble_control_regression/test_indication_submission_is_not_confirmation-main.inc')
        with tempfile.TemporaryDirectory() as tmp:
            c = pathlib.Path(tmp) / 'callback.c'
            c.write_text(fixture + branch + main)
            result = subprocess.run([shutil.which('cc') or 'cc', '-std=c11', str(c), '-o', tmp+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([tmp+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_native_cookie_detach_late_callback_and_host_barrier(self):
        import re
        source = BLE.read_text()
        names = ['ble_active_state_bind', 'ble_gatt_state_bind',
                 'ble_active_state_acquire', 'ble_future_state_hold',
                 'ble_future_state_drop', 'ble_operation_acquire',
                 'ble_native_state_complete', 'ble_native_states_quiesced',
                 'ble_gatt_callback_drop', 'ble_future_wake_state',
                 'ble_future_state_release', 'ble_exchange_mtu_callback',
                 'ble_discover_start_descriptors', 'ble_discover_complete',
                 'ble_discover_descriptor_callback', 'ble_discover_characteristic_callback',
                 'ble_discover_service_callback']
        bodies = []
        for name in names:
            match = re.search(r'static [^;{}]*\b' + name + r'\([^;{}]*\)\n\{', source)
            self.assertIsNotNone(match, name)
            end = source.index('\n}\n', match.start()) + 3
            bodies.append(source[match.start():end])
        with tempfile.TemporaryDirectory() as tmp:
            c = pathlib.Path(tmp) / 'lifecycle.c'
            c.write_text('#include "ble_lifecycle_test_support.h"\n' + '\n'.join(bodies) +
                         '\n#include "ble_lifecycle_cases.inc"\n')
            core = ROOT/'components/esp32_mquickjs/src/core'
            result = subprocess.run([shutil.which('cc') or 'cc', '-std=c11', '-g',
                '-I'+str(ROOT/'tests/c/support/ble'), '-I'+str(ROOT/'components/esp32_mquickjs/internal'),
                str(c), str(core/'esp32_mquickjs_wireless_core.c'),
                str(core/'esp32_mquickjs_native_pool.c'), '-o', tmp+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([tmp+'/test'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_scan_failed_cancel_retains_callback_visible_storage(self):
        from tests.support.c_source import extract as function
        from tests.support.native_compile import compile_run
        body = function(BLE.read_text(), 'ble_scan_destroy')
        compile_run(self, fixture_text('ble/test_ble_control_regression/test_scan_failed_cancel_retains_callback_visible_storage-02.inc') + body + fixture_text('ble/test_ble_control_regression/test_scan_failed_cancel_retains_callback_visible_storage.inc'))

    def test_timeout_failed_stop_retains_native_storage(self):
        from tests.support.c_source import extract as function
        from tests.support.native_compile import compile_run
        source = BLE.read_text()
        timeout = function(source, 'ble_future_on_timeout')
        for operation, owner, destroy, first, last in [
            ('SCAN', 'scanner', 'ble_scan_destroy', 'BLE_OP_SCAN', 'BLE_OP_ADVERTISE'),
            ('ADVERTISE', 'advertiser', 'ble_advertise_destroy', 'BLE_OP_ADVERTISE', 'BLE_OP_CONNECT'),
        ]:
            with self.subTest(operation=operation):
                # Compile the exact production timeout branch and destructor.
                branch = timeout[timeout.index('        case '+first+':'):
                                 timeout.index('        case '+last+':')]
                fixture = fixture_text('ble/test_ble_control_regression/test_timeout_failed_stop_retains_native_storage-fixture.inc')
                main = fixture_text('ble/test_ble_control_regression/test_timeout_failed_stop_retains_native_storage-main.inc').replace('OWNER', owner).replace('OPERATION', operation).replace('DESTROY', destroy)
                compile_run(self, fixture + branch + '} }\n' + function(source, destroy) + main)
