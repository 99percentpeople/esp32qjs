"""Production GAP disconnect guard/branch under saturated observation queues."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import sys
import unittest
from tests.support.native_compile import compile_run
from tests.support.c_source import extract as function
BLE=TEST_ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
SDK=fixture_text('ble/test_ble_disconnect_control_regression/sdk.inc')
class BleDisconnectControlRegression(unittest.TestCase):
    def production(self,kind="DISCONNECT",following="MTU"):
        source=BLE.read_text()
        callback=function(source,'ble_gap_event_callback_locked')
        a=callback.index('    if (event == NULL || (s_ble.lifecycle')
        guard=callback[a:callback.index('return 0;',a)+9]
        a=callback.index('    case BLE_GAP_EVENT_'+kind+':')
        branch=callback[a:callback.index('    case BLE_GAP_EVENT_'+following+':',a)]
        return 'static int dispatch(struct ble_gap_event *event) { ble_connection_slot_t *slot;uint16_t connection_index;esp32_mquickjs_future_driver_state_t *state=&operation;\n'+guard+'\nswitch(event->type) {\n'+branch+'}\nreturn 0; }\n'

    def test_future_terminal_precedes_full_observation_queue(self):
        compile_run(self,SDK+self.production()+fixture_text('ble/test_ble_disconnect_control_regression/test_future_terminal_precedes_full_observation_queue.inc'))

    def test_failed_lifecycle_still_accepts_native_cleanup_terminal(self):
        compile_run(self,SDK+self.production()+fixture_text('ble/test_ble_disconnect_control_regression/test_failed_lifecycle_still_accepts_native_cleanup_terminal.inc'))

    def test_pair_future_terminal_precedes_full_security_observation_queue(self):
        compile_run(self,SDK+self.production("ENC_CHANGE","PASSKEY_ACTION")+fixture_text('ble/test_ble_disconnect_control_regression/test_pair_future_terminal_precedes_full_security_observation_queue.inc'))
