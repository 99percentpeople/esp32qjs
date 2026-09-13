"""Exercise production advertiser start/release across SDK callback schedules."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import sys
import unittest

from tests.support.c_source import extract as function
from tests.support.native_compile import compile_run

BLE = TEST_ROOT / 'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
FIXTURE = fixture_text('ble/test_ble_advertise_callback_regression/fixture.inc')
MAIN = fixture_text('ble/test_ble_advertise_callback_regression/main.inc')

class BleAdvertiseCallbackRegression(unittest.TestCase):
    def test_queued_start_revalidates_adapter_and_advertiser_before_driver_mutation(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_advertise_callback_regression/test_queued_start_revalidates_adapter_and_advertiser_before_driver_mutation.inc'))

    def test_close_drains_entered_advertiser_callback_before_queue_release(self):
        import shutil
        import subprocess
        import tempfile
        main=fixture_text('ble/test_ble_advertise_callback_regression/test_close_drains_entered_advertiser_callback_before_queue_release-main.inc')
        with tempfile.TemporaryDirectory() as temporary:
            source=pathlib.Path(temporary)/'race.c'
            source.write_text('#define THREADED\n'+FIXTURE+self.production()+main)
            result=subprocess.run([shutil.which('cc') or 'cc','-std=c11','-pthread',
                str(source),'-o',temporary+'/test'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([temporary+'/test'],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)

    def production(self):
        source=BLE.read_text()
        return '\n'.join(function(source,name) for name in (
            'ble_reject_incoming_connection','ble_advertise_callback_bind',
            'ble_advertise_event_callback','ble_advertise_callbacks_quiesce',
            'ble_release_advertiser','ble_advertise_start'))

    def test_late_connect_rejects_only_unclaimed_connection_and_preserves_later_events(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_advertise_callback_regression/test_late_connect_rejects_only_unclaimed_connection_and_preserves_later_events.inc'))

    def test_rejection_failure_retains_native_owner_and_records_error(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_advertise_callback_regression/test_rejection_failure_retains_native_owner_and_records_error.inc'))

    def test_submission_failures_retire_cookie_and_exhaustion_skips_native_start(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_advertise_callback_regression/test_submission_failures_retire_cookie_and_exhaustion_skips_native_start.inc'))

    def test_synchronous_completion_is_not_overwritten(self):
        compile_run(self,FIXTURE+self.production()+MAIN.replace('IMMEDIATE','1'))

    def test_old_completion_does_not_stop_reopened_advertiser(self):
        compile_run(self,FIXTURE+self.production()+MAIN.replace('IMMEDIATE','0'))
