"""Deferred checked NDP submits using the production SDK helpers and ledger.

The native driver, host lookup, netif and data mutex are injected boundaries.
These cases do not establish native RX/timer retirement or RF acceptance.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.nan.test_wifi_nan_tx import NanTx, BASE


class NanDatapathOperations(unittest.TestCase):
    compile_case = NanTx.compile_case

    def helpers(self):
        return BOUNDARY + (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk_ndp.inc').read_text() + NATIVE

    def test_release_clears_failed_preclaim_and_reuses_deleted_native_address(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_ndp_operations/test_release_clears_failed_preclaim_and_reuses_deleted_native_address.inc'), native=self.helpers())

    def test_request_binds_before_early_confirm_and_preserves_native_failure(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_ndp_operations/test_request_binds_before_early_confirm_and_preserves_native_failure.inc'), native=self.helpers())

    def test_incoming_response_and_end_preserve_identity_and_submit_errors(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_ndp_operations/test_incoming_response_and_end_preserve_identity_and_submit_errors.inc'), native=self.helpers())


BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_ndp_operations/boundary.inc')

NATIVE = fixture_text('wifi/nan/test_wifi_nan_ndp_operations/native.inc')
