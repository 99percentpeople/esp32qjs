"""Deferred production NAN timer queue/reuse/cleanup cases; AST only in development."""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.nan.test_wifi_nan_tx import NanTx


class NanTimerIdentity(unittest.TestCase):
    compile_case = NanTx.compile_case

    def test_old_callbacks_and_queued_events_cannot_reach_reused_native_addresses(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_timer/test_old_callbacks_and_queued_events_cannot_reach_reused_native_addresses.inc'))

    def test_peer_delete_keeps_another_handshake_and_retains_failed_handle_after_node_free(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_timer/test_peer_delete_keeps_another_handshake_and_retains_failed_handle_after_node_free.inc'))

    def test_entered_post_retains_pool_and_identity_exhaustion_is_permanent(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_timer/test_entered_post_retains_pool_and_identity_exhaustion_is_permanent.inc'))
