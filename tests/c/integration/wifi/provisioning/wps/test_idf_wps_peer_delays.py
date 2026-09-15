"""Deferred exact peer-delay production helper tests; AST only in this wave."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run
ROOT=TEST_ROOT
sys.path.insert(0,str(ROOT/'scripts'))
from sdk_patches.wpa.wps.peer_delays import DELAYS

class WpsPeerDelays(unittest.TestCase):
    def run_case(self,main):
        compile_run(self,TYPES+DELAYS+BOUNDARIES+main)

    def test_old_ticket_cannot_delete_replacement_at_same_mac_or_address(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_peer_delays/test_old_ticket_cannot_delete_replacement_at_same_mac_or_address.inc'))

    def test_removal_oom_retains_obligation_and_close_retries_busy_peer(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_peer_delays/test_removal_oom_retains_obligation_and_close_retries_busy_peer.inc'))

    def test_deauth_failure_retains_raw_error_without_removing_other_peer(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_peer_delays/test_deauth_failure_retains_raw_error_without_removing_other_peer.inc'))

    def test_close_revokes_deauth_and_identity_exhaustion_never_wraps(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_peer_delays/test_close_revokes_deauth_and_identity_exhaustion_never_wraps.inc'))

TYPES=fixture_text('wifi/provisioning/wps/test_idf_wps_peer_delays/types.inc')
BOUNDARIES=fixture_text('wifi/provisioning/wps/test_idf_wps_peer_delays/boundaries.inc')
