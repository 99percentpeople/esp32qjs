"""Execute production connection mutation against controlled Host schedules."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import sys
import unittest
from tests.support.c_source import extract as function
from tests.support.native_compile import compile_run
BLE=TEST_ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
SDK=fixture_text('ble/test_ble_connection_mutation_regression/sdk.inc')
class BleConnectionMutationRegression(unittest.TestCase):
    def production(self):
        return function(BLE.read_text(),'ble_terminate_unclaimed_connection')

    def test_disconnect_and_reuse_cannot_interleave_with_native_termination(self):
        compile_run(self,SDK+self.production()+fixture_text('ble/test_ble_connection_mutation_regression/test_disconnect_and_reuse_cannot_interleave_with_native_termination.inc'))

    def test_stale_generation_and_already_closed_connection_skip_native_mutation(self):
        compile_run(self,SDK+self.production()+fixture_text('ble/test_ble_connection_mutation_regression/test_stale_generation_and_already_closed_connection_skip_native_mutation.inc'))

    def test_production_gap_entry_uses_same_lock_and_supports_callback_reentry(self):
        dispatch=fixture_text('ble/test_ble_connection_mutation_regression/test_production_gap_entry_uses_same_lock_and_supports_callback_reentry-dispatch.inc')
        compile_run(self,SDK+self.production()+dispatch+function(BLE.read_text(),'ble_gap_event_callback')+fixture_text('ble/test_ble_connection_mutation_regression/test_production_gap_entry_uses_same_lock_and_supports_callback_reentry.inc'))

    def test_registered_connection_start_paths_hold_guard_through_submission(self):
        for name in ['ble_connect_start','ble_pair_start','ble_exchange_mtu_start',
                     'ble_read_rssi_start','ble_discover_start','ble_gatt_read_start',
                     'ble_gatt_write_start','ble_subscribe_start','ble_server_notify_start']:
            with self.subTest(start=name):
                declarations=fixture_text('ble/test_ble_connection_mutation_regression/test_registered_connection_start_paths_hold_guard_through_submission-declarations.inc').replace('START',name)
                main=fixture_text('ble/test_ble_connection_mutation_regression/test_registered_connection_start_paths_hold_guard_through_submission-main.inc').replace('START',name)
                compile_run(self,SDK+declarations+function(BLE.read_text(),name)+main)
