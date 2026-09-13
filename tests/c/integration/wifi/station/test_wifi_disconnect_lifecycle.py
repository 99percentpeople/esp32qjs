"""Production disconnect/IP handoff, with SDK event delivery controlled by tests."""
from tests.support.fixtures import fixture_text
import unittest
from tests.support.native_compile import compile_run
import tests.c.integration.wifi.station.test_wifi_scan_lifecycle as scan_fixture

def record_connection_events(code):
    return code.replace(
        'static void wifi_queue_connect_event(unsigned g,int k,int r,const esp32_mquickjs_wifi_link_snapshot_t *link) { (void)g;(void)k;(void)r;(void)link; }',
        'static int connection_events,last_connection_kind;\nstatic void wifi_queue_connect_event(unsigned g,int k,int r,const esp32_mquickjs_wifi_link_snapshot_t *link) { (void)g;(void)r;(void)link;connection_events++;last_connection_kind=k; }')

class WiFiDisconnectLifecycle(unittest.TestCase):
    def test_late_ip_does_not_complete_disconnect(self):
        code = record_connection_events(scan_fixture.WiFiScanLifecycle().code())
        compile_run(self, code + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_late_ip_does_not_complete_disconnect.inc'))

    def test_disconnect_return_and_fifo_fence_both_precede_reuse(self):
        code = scan_fixture.WiFiScanLifecycle().code().replace(
            'wifi_process_driver_event(&done);\n    }\n    return disconnect_error;', fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_disconnect_return_and_fifo_fence_both_precede_reuse-code.inc'))
        compile_run(self, code + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_disconnect_return_and_fifo_fence_both_precede_reuse.inc'))

    def test_queue_full_retries_only_marker_and_rejects_old_epoch(self):
        compile_run(self, record_connection_events(scan_fixture.WiFiScanLifecycle().code()) + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_queue_full_retries_only_marker_and_rejects_old_epoch.inc'))

    def test_missing_terminal_native_error_and_netif_up_keep_quarantine(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_missing_terminal_native_error_and_netif_up_keep_quarantine.inc'))

    def test_epoch_exhaustion_cannot_be_cleared_by_old_marker(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_epoch_exhaustion_cannot_be_cleared_by_old_marker.inc'))

    def test_teardown_preserves_quarantine_and_old_cancel_cannot_touch_new_owner(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_teardown_preserves_quarantine_and_old_cancel_cannot_touch_new_owner.inc'))

    def test_connect_cannot_adopt_an_unregistered_native_operation(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_connect_cannot_adopt_an_unregistered_native_operation.inc'))

    def test_disconnect_after_terminal_does_not_wait_for_a_second_event(self):
        source = scan_fixture.WIFI.read_text()
        code = scan_fixture.WiFiScanLifecycle().code().replace(
            'static int esp32_mquickjs_wifi_start_disconnect(bool *pending) { *pending=false;return 0; }',
            'int esp32_mquickjs_wifi_start_disconnect(bool *pending);')
        code += scan_fixture.extract(source, 'esp32_mquickjs_wifi_start_disconnect')
        compile_run(self, code + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_disconnect_after_terminal_does_not_wait_for_a_second_event.inc'))

    def test_reconnect_changes_config_only_after_native_handoff(self):
        import tests.c.integration.wifi.station.test_wifi_connect_timer as timer_fixture
        compile_run(self, record_connection_events(timer_fixture.WiFiConnectTimer().barrier_code()) + fixture_text('wifi/station/test_wifi_disconnect_lifecycle/test_reconnect_changes_config_only_after_native_handoff.inc'))
