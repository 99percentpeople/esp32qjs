"""Deferred production NAN DataPath worker/owner/receive/timeout regression cases."""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.nan.test_wifi_nan_session import NanSessionLifecycle


class NanDataPathLifecycle(unittest.TestCase):
    compile_case = NanSessionLifecycle.compile_case

    def test_outgoing_early_completion_and_close_wait_for_exact_native_retirement(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_path/test_outgoing_early_completion_and_close_wait_for_exact_native_retirement.inc'))

    def test_incoming_reservation_rollback_and_explicit_response_ignore_full_observation_queue(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_path/test_incoming_reservation_rollback_and_explicit_response_ignore_full_observation_queue.inc'))

    def test_unanswered_request_and_allocation_failure_decline_without_js_owner(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_path/test_unanswered_request_and_allocation_failure_decline_without_js_owner.inc'))

    def test_public_timeout_preserves_cleanup_and_failed_native_end_closes_parent(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_path/test_public_timeout_preserves_cleanup_and_failed_native_end_closes_parent.inc'))


HELPER = fixture_text('wifi/nan/test_wifi_nan_path/helper.inc')
