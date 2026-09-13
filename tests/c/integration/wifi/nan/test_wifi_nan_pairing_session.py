"""Deferred production Pairing/Service/Session ownership and scheduling cases.

Only Radio native commands and the existing worker/clock boundary are injected.
No independent pairing state machine is used as the subject under test.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.nan.test_wifi_nan_session import NanSessionLifecycle


class NanPairingSession(unittest.TestCase):
    compile_case = NanSessionLifecycle.compile_case

    def test_confirmation_is_explicit_pin_copy_and_final_tx_precede_ready(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_pairing_session/test_confirmation_is_explicit_pin_copy_and_final_tx_precede_ready.inc'), security=True, pairing=True)

    def test_rejection_and_unanswered_deadline_never_start_authentication(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_pairing_session/test_rejection_and_unanswered_deadline_never_start_authentication.inc'), security=True, pairing=True)

    def test_native_failure_and_full_queue_keep_cleanup_and_service_owner(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_pairing_session/test_native_failure_and_full_queue_keep_cleanup_and_service_owner.inc'), security=True, pairing=True)

    def test_parent_shutdown_does_not_wait_for_pairing_to_stop_before_stop(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_pairing_session/test_parent_shutdown_does_not_wait_for_pairing_to_stop_before_stop.inc'), security=True, pairing=True)

    def test_completed_before_deadline_is_not_changed_by_delayed_worker(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_pairing_session/test_completed_before_deadline_is_not_changed_by_delayed_worker.inc'), security=True, pairing=True)

    def test_detached_service_handles_and_cache_read_coalescing(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_pairing_session/test_detached_service_handles_and_cache_read_coalescing.inc'), security=True, pairing=True)

    def test_verification_requires_confirmation_and_cache_commit_precedes_ready(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_pairing_session/test_verification_requires_confirmation_and_cache_commit_precedes_ready.inc'), security=True, pairing=True)

    def test_allocation_failure_activation_failure_and_identity_exhaustion(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_pairing_session/test_allocation_failure_activation_failure_and_identity_exhaustion.inc'), security=True, pairing=True)


PAIRING_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_pairing_session/pairing_boundary.inc')

HELPER = fixture_text('wifi/nan/test_wifi_nan_pairing_session/helper.inc')
