"""Production Wi-Fi admission and lifetime; Radio boundary has controlled errors."""
from tests.support.fixtures import fixture_text
import unittest
import tests.c.integration.wifi.station.test_wifi_scan_lifecycle as scan_fixture
import tests.c.integration.wifi.station.test_wifi_connect_timer as timer_fixture
from tests.support.native_compile import compile_run

class WiFiRadioAdmission(unittest.TestCase):
    def test_scan_conflict_rejected_before_driver_submission(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + fixture_text('wifi/config/test_wifi_radio_admission/test_scan_conflict_rejected_before_driver_submission.inc'))

    def test_connect_conflict_does_not_disconnect_or_replace_config(self):
        compile_run(self, timer_fixture.WiFiConnectTimer().barrier_code() + fixture_text('wifi/config/test_wifi_radio_admission/test_connect_conflict_does_not_disconnect_or_replace_config.inc'))

    def test_scan_reservation_survives_cancel_and_failed_result_cleanup(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + fixture_text('wifi/config/test_wifi_radio_admission/test_scan_reservation_survives_cancel_and_failed_result_cleanup.inc'))

    def test_scan_submission_failure_releases_reservation(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + fixture_text('wifi/config/test_wifi_radio_admission/test_scan_submission_failure_releases_reservation.inc'))

    def test_scan_callback_before_submission_return_retains_reservation(self):
        code = scan_fixture.WiFiScanLifecycle().code().replace(
            'starts++;return start_error;', fixture_text('wifi/config/test_wifi_radio_admission/test_scan_callback_before_submission_return_retains_reservation-code.inc'))
        code = code.replace('static int esp_wifi_scan_start(', 'static void wifi_release_radio_operation(void);\nstatic int esp_wifi_scan_start(')
        compile_run(self, code + fixture_text('wifi/config/test_wifi_radio_admission/test_scan_callback_before_submission_return_retains_reservation.inc'))

    def test_connect_timeout_retains_reservation_until_ip_fence(self):
        compile_run(self, timer_fixture.WiFiConnectTimer().barrier_code() + fixture_text('wifi/config/test_wifi_radio_admission/test_connect_timeout_retains_reservation_until_ip_fence.inc'))

    def test_connect_config_failure_and_success_release_at_native_boundary(self):
        code = timer_fixture.WiFiConnectTimer().barrier_code().replace(
            'configs++;return 0;', 'configs++;return config_error;').replace(
            'static int esp_wifi_set_config(', 'static int config_error;\nstatic int esp_wifi_set_config(')
        from tests.support.wireless_vm_fixture import extract
        original_connect = extract(code, 'esp_wifi_connect')
        code = code.replace(original_connect, fixture_text('wifi/config/test_wifi_radio_admission/test_connect_config_failure_and_success_release_at_native_boundary-code.inc'))
        compile_run(self, code + fixture_text('wifi/config/test_wifi_radio_admission/test_connect_config_failure_and_success_release_at_native_boundary.inc'))

    def test_teardown_keeps_reservation_until_native_handoff_drains(self):
        code = timer_fixture.WiFiConnectTimer().barrier_code()
        code += scan_fixture.extract(scan_fixture.WIFI.read_text(), 'wifi_finish_runtime_cleanup') + scan_fixture.extract(scan_fixture.WIFI.read_text(), 'esp32_mquickjs_deinit_wifi_runtime')
        compile_run(self, code + fixture_text('wifi/config/test_wifi_radio_admission/test_teardown_keeps_reservation_until_native_handoff_drains.inc'))

    def test_connect_preparation_precedes_future_registration(self):
        compile_run(self, scan_fixture.WiFiScanLifecycle().code() + fixture_text('wifi/config/test_wifi_radio_admission/test_connect_preparation_precedes_future_registration.inc'))
