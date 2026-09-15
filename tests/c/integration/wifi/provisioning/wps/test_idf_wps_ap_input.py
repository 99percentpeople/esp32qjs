"""Deferred production AP input pin/IE ownership fixtures; AST only."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run
ROOT=TEST_ROOT
sys.path.insert(0,str(ROOT/'scripts'))
from sdk_patches.wpa.wps.ap_input import ACQUIRE,RX,ASSOC


class WpsAPInput(unittest.TestCase):
    def run_case(self,main):
        compile_run(self,TYPES+ACQUIRE+RX+ASSOC+BOUNDARIES+main)

    def test_stale_driver_pointer_busy_peer_and_callback_pin(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_input/test_stale_driver_pointer_busy_peer_and_callback_pin.inc'))

    def test_close_blocks_wps_but_preserves_wpa_keys_and_station_owner(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_input/test_close_blocks_wps_but_preserves_wpa_keys_and_station_owner.inc'))

    def test_disabled_missing_and_overlap_association_release_every_ie(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_input/test_disabled_missing_and_overlap_association_release_every_ie.inc'))

    def test_replacement_oom_malformed_input_and_response_failure(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_input/test_replacement_oom_malformed_input_and_response_failure.inc'))

TYPES=fixture_text('wifi/provisioning/wps/test_idf_wps_ap_input/types.inc')
BOUNDARIES=fixture_text('wifi/provisioning/wps/test_idf_wps_ap_input/boundaries.inc')
