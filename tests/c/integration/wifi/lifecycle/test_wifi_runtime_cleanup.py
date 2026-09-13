"""Production Wi-Fi runtime retirement, including delayed native terminals."""
from tests.support.fixtures import fixture_text
import unittest
import tests.c.integration.wifi.station.test_wifi_scan_lifecycle as fixture
from tests.support.native_compile import compile_run
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT

class WiFiRuntimeCleanup(unittest.TestCase):
    def test_established_connection_is_disconnected_on_runtime_teardown(self):
        compile_run(self,fixture.WiFiScanLifecycle().code()+fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_established_connection_is_disconnected_on_runtime_teardown.inc'))

    def test_disconnect_terminal_and_ip_fence_both_precede_resource_release(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_disconnect_terminal_and_ip_fence_both_precede_resource_release.inc'))

    def test_attach_waits_for_native_events_without_repeating_disconnect(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_attach_waits_for_native_events_without_repeating_disconnect.inc'))

    def test_missing_terminal_bounds_wait_and_preserves_native_storage(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_missing_terminal_bounds_wait_and_preserves_native_storage.inc'))

    def test_scan_terminal_and_ap_list_cleanup_precede_resource_release(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_scan_terminal_and_ap_list_cleanup_precede_resource_release.inc'))

    def test_idle_teardown_detaches_runtime_before_entered_callback_exits(self):
        compile_run(self, fixture.WiFiScanLifecycle().code()+fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_idle_teardown_detaches_runtime_before_entered_callback_exits.inc'))

    def test_detached_runtime_is_not_recovered_from_global_active_runtime(self):
        body=fixture.extract(fixture.WIFI.read_text(), 'wifi_queue_connect_event')
        compile_run(self, fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_detached_runtime_is_not_recovered_from_global_active_runtime-03.inc') + structure((COMPONENT / 'internal/esp32_mquickjs_types.h').read_text(), 'esp32_mquickjs_wifi_link_snapshot_t') + structure((COMPONENT / 'internal/esp32_mquickjs_wifi.h').read_text(), 'esp32_mquickjs_wifi_connect_event_t') + fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_detached_runtime_is_not_recovered_from_global_active_runtime-02.inc') + body + fixture_text('wifi/lifecycle/test_wifi_runtime_cleanup/test_detached_runtime_is_not_recovered_from_global_active_runtime.inc'))
