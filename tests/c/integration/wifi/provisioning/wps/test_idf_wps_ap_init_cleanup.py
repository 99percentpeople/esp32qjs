"""Deferred production AP initialization failure cleanup; AST only."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import sys
import unittest
from tests.c.integration.wifi.provisioning.wps.test_idf_wps_ap_result import WpsAPResult
ROOT=TEST_ROOT
sys.path.insert(0,str(ROOT/'scripts'))
from sdk_patches.wpa.wps.ap_init_cleanup import FAILURE


class WpsAPInitCleanup(unittest.TestCase):
    run_case=WpsAPResult.run_case

    def test_every_failed_cleanup_stage_retains_result_and_retries_only_suffix(self):
        self.run_case(BOUNDARIES+FAILURE+fixture_text('wifi/provisioning/wps/test_idf_wps_ap_init_cleanup/test_every_failed_cleanup_stage_retains_result_and_retries_only_suffix.inc'))

    def test_managed_initializer_error_keeps_result_and_cleanup_obligation(self):
        self.run_case(BOUNDARIES+FAILURE+fixture_text('wifi/provisioning/wps/test_idf_wps_ap_init_cleanup/test_managed_initializer_error_keeps_result_and_cleanup_obligation.inc'))

BOUNDARIES=fixture_text('wifi/provisioning/wps/test_idf_wps_ap_init_cleanup/boundaries.inc')
