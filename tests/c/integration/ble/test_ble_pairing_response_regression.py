"""Production pairing response rejects identity changes during JS conversion."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import sys
import unittest
from tests.support.c_source import extract as function
from tests.support.native_compile import compile_run
BLE=TEST_ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
SDK=fixture_text('ble/test_ble_pairing_response_regression/sdk.inc')
class BlePairingResponseRegression(unittest.TestCase):
    def production(self):
        return function(BLE.read_text().replace('JSValue js_ble_connection_respond_pairing(',
            'static JSValue js_ble_connection_respond_pairing('),'js_ble_connection_respond_pairing')

    def test_disconnect_generation_request_change_and_expiry_during_conversion_are_rejected(self):
        compile_run(self,SDK+self.production()+fixture_text('ble/test_ble_pairing_response_regression/test_disconnect_generation_request_change_and_expiry_during_conversion_are_rejected.inc'))

    def test_numeric_comparison_requires_explicit_boolean_and_preserves_rejection(self):
        compile_run(self,SDK+self.production()+fixture_text('ble/test_ble_pairing_response_regression/test_numeric_comparison_requires_explicit_boolean_and_preserves_rejection.inc'))
