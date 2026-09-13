"""Production AP/STA coordinator admission and cleanup suffix; phase-run pending."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import unittest
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract as function

ROOT = TEST_ROOT
WIFI = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'
AP = WIFI.with_name('esp32_mquickjs_wifi_ap.c')


class WiFiConfigurationCleanup(unittest.TestCase):
    def code(self):
        wifi, ap = WIFI.read_text(), AP.read_text()
        body = ''.join(function(ap, name) for name in ['wifi_ap_retire_netif',
            'wifi_ap_cleanup', 'esp32_mquickjs_wifi_ap_begin_configuration',
            'esp32_mquickjs_wifi_ap_begin_selected_configuration',
            'wifi_ap_retire_for_token', 'esp32_mquickjs_wifi_ap_retire_for_configuration',
            'esp32_mquickjs_wifi_ap_retire_for_recovery'])
        body += ''.join(function(wifi, name) for name in ['esp32_mquickjs_wifi_connection_reserved_locked', 'wifi_helpers_idle',
            'esp32_mquickjs_wifi_retire_for_recovery',
            'wifi_begin_configuration_cleanup', 'wifi_begin_selected_configuration', 'wifi_finish_configuration_cleanup',
            'esp32_mquickjs_wifi_configuration_pending', 'esp32_mquickjs_wifi_cleanup_ap_configuration'])
        # AP exports are non-static in production; use the shared extractor for them.
        return BOUNDARIES + body

    def test_exact_handoff_retains_token_through_each_failed_suffix(self):
        compile_run(self, self.code() + MAIN)

    def test_recovery_helper_suffix_preserves_lifecycle_until_native_owner_retires(self):
        compile_run(self, self.code() + fixture_text('wifi/config/test_wifi_configuration_cleanup/test_recovery_helper_suffix_preserves_lifecycle_until_native_owner_retires.inc'))

    def test_vendor_cleanup_failure_keeps_coordinator_before_other_suffixes(self):
        compile_run(self, self.code() + fixture_text('wifi/config/test_wifi_configuration_cleanup/test_vendor_cleanup_failure_keeps_coordinator_before_other_suffixes.inc'))

    def test_allow_disconnect_is_checked_before_any_owner_release(self):
        compile_run(self, self.code() + fixture_text('wifi/config/test_wifi_configuration_cleanup/test_allow_disconnect_is_checked_before_any_owner_release.inc'))

    def test_selected_admission_failure_keeps_all_helpers_and_owners(self):
        compile_run(self, self.code() + fixture_text('wifi/config/test_wifi_configuration_cleanup/test_selected_admission_failure_keeps_all_helpers_and_owners.inc'))


BOUNDARIES = fixture_text('wifi/config/test_wifi_configuration_cleanup/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_configuration_cleanup/main.inc')
