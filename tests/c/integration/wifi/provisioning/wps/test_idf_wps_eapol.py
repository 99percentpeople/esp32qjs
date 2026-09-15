"""Deferred production EAPOL timer identity and peer-lock regressions.

Implementation wave: AST only. Production TIMERS and PEER_RELEASE are compiled
with event-loop, AP owner and semaphore boundaries when the Wi-Fi suite runs.
No replacement EAP state machine or claim of complete SDK/RF qualification.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from sdk_patches.wpa.wps.eapol import TIMERS, PEER_RELEASE


class WpsEapolTimers(unittest.TestCase):
    def run_case(self, main):
        compile_run(self, TYPES + TIMERS + PEER_RELEASE + BOUNDARIES + main)

    def test_consumed_cancelled_and_reused_address_tickets_cannot_dispatch(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol/test_consumed_cancelled_and_reused_address_tickets_cannot_dispatch.inc'))

    def test_sae_busy_retry_is_numeric_and_removed_peer_cannot_be_touched(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol/test_sae_busy_retry_is_numeric_and_removed_peer_cannot_be_touched.inc'))

    def test_whole_callback_pins_peer_and_deferred_delete_consumes_lock(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol/test_whole_callback_pins_peer_and_deferred_delete_consumes_lock.inc'))

    def test_busy_retry_allocation_failure_stops_exact_peer_and_preserves_other(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol/test_busy_retry_allocation_failure_stops_exact_peer_and_preserves_other.inc'))

    def test_timer_oom_and_exhaustion_fail_only_this_peer_and_stop_revokes_both(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol/test_timer_oom_and_exhaustion_fail_only_this_peer_and_stop_revokes_both.inc'))

    def test_pending_step_coalesces_and_close_drops_old_callbacks(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_eapol/test_pending_step_coalesces_and_close_drops_old_callbacks.inc'))


TYPES = fixture_text('wifi/provisioning/wps/test_idf_wps_eapol/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_eapol/boundaries.inc')
