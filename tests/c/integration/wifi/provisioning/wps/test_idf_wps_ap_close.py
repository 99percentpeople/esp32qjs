"""Deferred AP close regressions using production helpers and SDK functions.

AST only during implementation. Callback/driver boundaries inject scheduling and
errors; no independent cleanup model or assertion of EAP/native drain.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.c.integration.wifi.provisioning.wps.test_idf_wps_ap_result import WpsAPResult
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function
from patch_idf_wps_registrar import patch_source


class WpsAPClose(unittest.TestCase):
    run_case = WpsAPResult.run_case

    def test_native_protocol_activity_can_finish_after_result_but_not_after_close(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_close/test_native_protocol_activity_can_finish_after_result_but_not_after_close.inc'))

    def test_each_close_failure_retries_only_unfinished_suffix(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_close/test_each_close_failure_retries_only_unfinished_suffix.inc'))

    def test_reentrant_close_retains_context_and_exact_leave_after_terminal(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_close/test_reentrant_close_retains_context_and_exact_leave_after_terminal.inc'))

    def test_retained_prefix_is_not_permission_to_free_and_depth_fault_is_sticky(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_close/test_retained_prefix_is_not_permission_to_free_and_depth_fault_is_sticky.inc'))


class WpsAPHostClose(unittest.TestCase):
    def test_original_ignores_close_failure_patched_retains_ap_and_station_is_untouched(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        relative = 'esp_supplicant/src/esp_hostap.c'
        original = (Path(sdk) / 'components/wpa_supplicant' / relative).read_bytes()
        for patched in (False, True):
            source = patch_source(relative, original).decode() if patched else original.decode()
            code = HOST_BOUNDARIES
            if patched:
                code += function(source, 'esp32qjs_hostap_close_wps')
            code += function(source, 'hostapd_cleanup') + function(source, 'hostap_deinit')
            code += fixture_text('wifi/provisioning/wps/test_idf_wps_ap_close/test_original_ignores_close_failure_patched_retains_ap_and_station_is_untouched.inc').replace('PATCHED', '1' if patched else '0')
            with self.subTest(patched=patched): compile_run(self, code)


HOST_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_ap_close/host_boundaries.inc')
