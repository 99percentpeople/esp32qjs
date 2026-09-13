"""Production scan cancellation and terminal callbacks with controlled SDK timing."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
WIFI = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'
FUTURE = WIFI.with_name('esp32_mquickjs_wifi_future.c')

def extract(source, name):
    import re
    match = re.search(r'(?:static )?[\w *]+\b' + name + r'\([^;{}]*\)\n\{', source)
    assert match, name
    return source[match.start():source.index('\n}\n', match.start()) + 3]

SDK = fixture_text('wifi/station/test_wifi_scan_lifecycle/sdk.inc')

SDK += extract(WIFI.read_text(), 'esp32_mquickjs_wifi_connection_reserved_locked')


class WiFiScanLifecycle(unittest.TestCase):
    def code(self):
        source = WIFI.read_text()
        helpers = ''
        for name in ['wifi_release_radio_operation','wifi_prepare_radio_operation','esp32_mquickjs_wifi_drain_scan', 'esp32_mquickjs_wifi_stop_scan_for_results', 'esp32_mquickjs_wifi_scan_results_consumed', 'wifi_start_scan_reserved','esp32_mquickjs_wifi_start_scan', 'esp32_mquickjs_wifi_cancel_scan', 'wifi_begin_disconnect_locked', 'wifi_finish_disconnect_locked', 'wifi_post_disconnect_fence', 'wifi_request_disconnect', 'esp32_mquickjs_wifi_cancel_connect']:
            if name + '(' in source:
                helpers += extract(source, name)
        return SDK + helpers + extract(source, 'wifi_process_driver_event') + extract(source, 'wifi_finish_runtime_cleanup') + extract(source, 'esp32_mquickjs_deinit_wifi_runtime') + ''.join(extract(FUTURE.read_text(), name) for name in ['wifi_future_start', 'wifi_future_cancel', 'wifi_future_destroy'])

    def test_stop_for_partial_results_retains_lane_and_does_not_clear_consumed_list(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_scan_lifecycle/test_partial_results.inc'))

    def test_cancel_retains_native_scan_until_late_terminal(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_scan_lifecycle/test_cancel_retains_native_scan_until_late_terminal.inc'))

    def test_future_admission_waits_for_terminal_and_result_cleanup(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_scan_lifecycle/test_future_admission_waits_for_terminal_and_result_cleanup.inc'))

    def test_stop_failure_and_completion_inside_stop_do_not_reuse_storage(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_scan_lifecycle/test_stop_failure_and_completion_inside_stop_do_not_reuse_storage.inc'))

    def test_destroy_completed_unread_scan_frees_results_and_start_failure_rolls_back(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_scan_lifecycle/test_destroy_completed_unread_scan_frees_results_and_start_failure_rolls_back.inc'))

    def test_generation_exhaustion_and_prepared_cancel_have_no_native_effect(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_scan_lifecycle/test_generation_exhaustion_and_prepared_cancel_have_no_native_effect.inc'))

    def test_runtime_teardown_keeps_cancelled_scan_quarantined_across_attach(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_scan_lifecycle/test_runtime_teardown_keeps_cancelled_scan_quarantined_across_attach.inc'))
