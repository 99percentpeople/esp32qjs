"""Deferred bootstrap cases using the production Session/Pairing/message worker.

Only the native command/callback, allocation, clock and worker dispatch boundaries
are injected. These fixtures are not RF evidence and are not run on import.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.nan.test_wifi_nan_session import NanSessionLifecycle
from tests.c.integration.wifi.nan.test_wifi_nan_pairing_session import HELPER


PUBLISH_HELPER = (HELPER.replace('.subscribe=', '.publish=')
                  .replace('.config.subscribe', '.config.publish')
                  .replace('->config.subscribe', '->config.publish')
                  .replace('create(s,false,&cfg', 'create(s,true,&cfg')
                  .replace('WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD', 'WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY'))

BOOTSTRAP_HELPER = fixture_text('wifi/nan/test_wifi_nan_bootstrap/bootstrap_helper.inc')


class NanBootstrap(unittest.TestCase):
    compile_case = NanSessionLifecycle.compile_case

    def test_bootstrap_history_is_bounded_and_close_before_submission_cannot_send(self):
        self.compile_case(HELPER + BOOTSTRAP_HELPER + fixture_text('wifi/nan/test_wifi_nan_bootstrap/test_bootstrap_history_is_bounded_and_close_before_submission_cannot_send.inc'), security=True, pairing=True)

    def test_outgoing_requires_local_consent_exact_response_and_request_recycle(self):
        self.compile_case(HELPER + BOOTSTRAP_HELPER + fixture_text('wifi/nan/test_wifi_nan_bootstrap/test_outgoing_requires_local_consent_exact_response_and_request_recycle.inc'), security=True, pairing=True)

    def test_incoming_survives_full_observation_queue_and_receive_conversion_abort(self):
        self.compile_case(PUBLISH_HELPER + BOOTSTRAP_HELPER + fixture_text('wifi/nan/test_wifi_nan_bootstrap/test_incoming_survives_full_observation_queue_and_receive_conversion_abort.inc'), security=True, pairing=True)

    def test_default_reject_uses_arrival_deadline_and_waits_for_reject_recycle(self):
        self.compile_case(PUBLISH_HELPER + BOOTSTRAP_HELPER + fixture_text('wifi/nan/test_wifi_nan_bootstrap/test_default_reject_uses_arrival_deadline_and_waits_for_reject_recycle.inc'), security=True, pairing=True)

    def test_expired_unadopted_and_allocation_failed_requests_release_capture_owner(self):
        self.compile_case(PUBLISH_HELPER + BOOTSTRAP_HELPER + fixture_text('wifi/nan/test_wifi_nan_bootstrap/test_expired_unadopted_and_allocation_failed_requests_release_capture_owner.inc'), security=True, pairing=True)

    def test_comeback_and_close_quarantine_sent_request_until_native_recycle(self):
        self.compile_case(HELPER + BOOTSTRAP_HELPER + fixture_text('wifi/nan/test_wifi_nan_bootstrap/test_comeback_and_close_quarantine_sent_request_until_native_recycle.inc'), security=True, pairing=True)

    def test_runtime_stop_retires_inflight_bootstrap_before_detaching_service(self):
        self.compile_case(HELPER + BOOTSTRAP_HELPER + fixture_text('wifi/nan/test_wifi_nan_bootstrap/test_runtime_stop_retires_inflight_bootstrap_before_detaching_service.inc'), security=True, pairing=True)
