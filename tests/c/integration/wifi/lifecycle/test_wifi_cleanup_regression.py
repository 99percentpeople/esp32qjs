"""Failure injection against the production Wi-Fi helper cleanup function."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import unittest
from tests.support.c_source import extract as function
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'

class WiFiCleanupRegression(unittest.TestCase):
    def code(self):
        body = ''.join(function(SOURCE.read_text(), name) for name in ['wifi_reset_helper_state', 'wifi_retire_station_netif', 'wifi_cleanup_helper', 'wifi_cleanup_failed_init'])
        return fixture_text('wifi/lifecycle/test_wifi_cleanup_regression/code.inc') + body

    def test_failed_cleanup_retains_callback_storage_and_retries_only_suffix(self):
        compile_run(self, self.code() + fixture_text('wifi/lifecycle/test_wifi_cleanup_regression/test_failed_cleanup_retains_callback_storage_and_retries_only_suffix.inc'))

    def test_callback_timeout_keeps_storage_and_does_not_repeat_unregistration(self):
        code = self.code().replace(
            'static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { return fallback; }',
            'static unsigned remaining=2; static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { (void)fallback;return remaining; }')
        code = code.replace('static void vTaskDelay(int n) { (void)n;assert(0); }',
                            'static void vTaskDelay(int n) { assert(n==1 && remaining);remaining--; }')
        compile_run(self, code + fixture_text('wifi/lifecycle/test_wifi_cleanup_regression/test_callback_timeout_keeps_storage_and_does_not_repeat_unregistration.inc'))

    def test_runtime_attach_does_not_reuse_pending_native_resources(self):
        source = SOURCE.read_text()
        start = source.index('bool esp32_mquickjs_init_wifi_runtime(')
        body = source[start:source.index('\n}\n', start) + 3]
        compile_run(self, fixture_text('wifi/lifecycle/test_wifi_cleanup_regression/test_runtime_attach_does_not_reuse_pending_native_resources-02.inc') + body + fixture_text('wifi/lifecycle/test_wifi_cleanup_regression/test_runtime_attach_does_not_reuse_pending_native_resources.inc'))
