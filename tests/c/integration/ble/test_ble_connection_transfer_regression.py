"""Execute production BLE connection ownership and queue handoff paths."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import sys
import unittest

from tests.support.c_source import extract as function
from tests.support.native_compile import compile_run
BLE=TEST_ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
FIXTURE=fixture_text('ble/test_ble_connection_transfer_regression/fixture.inc')

class BleConnectionTransferRegression(unittest.TestCase):
    def production(self):
        source=BLE.read_text()
        return function(source,'ble_terminate_unclaimed_connection')+function(source,'ble_connect_finish')+function(source,'ble_connect_destroy')

    def test_js_allocation_failure_does_not_transfer_native_connection(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_connection_transfer_regression/test_js_allocation_failure_does_not_transfer_native_connection.inc'))

    def test_failed_terminate_preserves_slot_until_native_terminal(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_connection_transfer_regression/test_failed_terminate_preserves_slot_until_native_terminal.inc'))

    def test_successful_handoff_and_stale_state_do_not_disconnect_live_owner(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_connection_transfer_regression/test_successful_handoff_and_stale_state_do_not_disconnect_live_owner.inc'))

    def test_cleanup_retry_releases_only_after_native_absence_is_confirmed(self):
        compile_run(self,FIXTURE+self.production()+fixture_text('ble/test_ble_connection_transfer_regression/test_cleanup_retry_releases_only_after_native_absence_is_confirmed.inc'))

    def test_runtime_teardown_retries_unclaimed_termination_before_callback_barrier(self):
        source=BLE.read_text().replace('bool esp32_mquickjs_deinit_ble_runtime(',
                                      'static bool esp32_mquickjs_deinit_ble_runtime(')
        fixture=FIXTURE.replace('unsigned max_connections;',
            'int lifecycle;void *runtime;struct { bool active; } scanner,advertiser;unsigned max_connections;')
        compile_run(self,fixture+self.production()+fixture_text('ble/test_ble_connection_transfer_regression/test_runtime_teardown_retries_unclaimed_termination_before_callback_barrier-02.inc')+function(source,'esp32_mquickjs_deinit_ble_runtime')+fixture_text('ble/test_ble_connection_transfer_regression/test_runtime_teardown_retries_unclaimed_termination_before_callback_barrier.inc'))

    def test_closed_result_does_not_rebind_a_reused_native_handle(self):
        fixture=FIXTURE.replace('bool transferred;', 'bool transferred;struct { int val; } owner_ref;').replace('static int result=JS_EXCEPTION,', 'static int callback_rebinds;\nstatic int result=JS_EXCEPTION,').replace('(void)handle;(void)cb;(void)arg;return 0;', '(void)handle;(void)cb;(void)arg;callback_rebinds++;return 0;')
        compile_run(self,fixture+fixture_text('ble/test_ble_connection_transfer_regression/test_closed_result_does_not_rebind_a_reused_native_handle-02.inc')+function(BLE.read_text(),'ble_connection_close_finish')+fixture_text('ble/test_ble_connection_transfer_regression/test_closed_result_does_not_rebind_a_reused_native_handle.inc'))
