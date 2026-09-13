"""Exercise production Wi-Fi timeout dispatch and Future cancellation."""
from tests.support.fixtures import fixture_text
import unittest
from tests.support.native_compile import compile_run
from tests.c.integration.wifi.station.test_wifi_scan_lifecycle import SDK, WIFI, FUTURE, extract
from tests.support.wifi_connection_counter_fixture import connection_counter_code

class WiFiConnectTimer(unittest.TestCase):
    def code(self):
        sdk = SDK.replace('int kind,reason;unsigned status;', 'int kind,reason;unsigned status,generation;')
        sdk += '\n#define disconnects disconnect_calls\n'
        source = WIFI.read_text()
        helpers = ''.join(extract(source,n) for n in ['wifi_release_radio_operation','wifi_prepare_radio_operation','esp32_mquickjs_wifi_drain_scan','wifi_start_scan_reserved','esp32_mquickjs_wifi_start_scan','esp32_mquickjs_wifi_cancel_scan','wifi_begin_disconnect_locked','wifi_finish_disconnect_locked','wifi_post_disconnect_fence','wifi_request_disconnect','esp32_mquickjs_wifi_cancel_connect'])
        return sdk + helpers + extract(source,'wifi_process_driver_event') + extract(FUTURE.read_text(),'wifi_future_cancel')

    def test_old_timeout_cannot_disconnect_new_operation(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_connect_timer/test_old_timeout_cannot_disconnect_new_operation.inc'))

    def test_prepared_connect_cancel_does_not_disconnect_existing_link(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_connect_timer/test_prepared_connect_cancel_does_not_disconnect_existing_link.inc'))

    def barrier_code(self):
        sdk = SDK.replace('static void wifi_stop_connect_timeout_timer(void) {}', '')
        sdk = sdk.replace('static int esp32_mquickjs_wifi_prepare_connect_timer(void) { return 0; }', '')
        sdk = sdk.replace('static int esp32_mquickjs_wifi_start_connect(const void *config,unsigned timeout) { (void)config;(void)timeout;return 0; }', '')
        sdk = sdk.replace('    const char *cleanup_stage;', '    void *connect_timeout_timer,*driver_event_queue;\n    const char *cleanup_stage;')
        sdk = sdk.replace('int last_disconnect_reason;esp32_mquickjs_wifi_link_snapshot_t link;', 'int last_disconnect_reason,connect_timer_error;char ssid[33];esp32_mquickjs_wifi_link_snapshot_t link;')
        sdk += '\n#define disconnects disconnect_calls\n'
        sdk += fixture_text('wifi/station/test_wifi_connect_timer/barrier_code.inc')
        source = WIFI.read_text()
        counters = fixture_text('wifi/station/test_wifi_connect_timer/barrier_code-counters.inc')
        return sdk + counters + connection_counter_code() + ''.join(extract(source,n) for n in [
            'wifi_stop_connect_timeout_timer','esp32_mquickjs_wifi_prepare_connect_timer',
            'wifi_publish_driver_event_from_callback','wifi_connect_timeout_cb',
            'wifi_release_radio_operation','wifi_prepare_radio_operation','esp32_mquickjs_wifi_drain_scan','wifi_start_scan_reserved','esp32_mquickjs_wifi_start_scan','esp32_mquickjs_wifi_cancel_scan',
            'wifi_begin_disconnect_locked','wifi_finish_disconnect_locked','wifi_post_disconnect_fence','wifi_request_disconnect','esp32_mquickjs_wifi_cancel_connect',
            'wifi_process_driver_event','wifi_driver_event_poller','wifi_start_connect_reserved','esp32_mquickjs_wifi_start_connect'])

    def test_observations_follow_actual_submission_and_reset_preserves_operation(self):
        compile_run(self, self.barrier_code() + fixture_text('wifi/station/test_wifi_connect_timer/test_observations_follow_actual_submission_and_reset_preserves_operation.inc'))

    def test_callback_barrier_failure_prevents_config_and_native_connect(self):
        compile_run(self, self.barrier_code() + fixture_text('wifi/station/test_wifi_connect_timer/test_callback_barrier_failure_prevents_config_and_native_connect.inc'))

    def test_callback_generation_survives_deferred_runtime_poll(self):
        compile_run(self, self.barrier_code() + fixture_text('wifi/station/test_wifi_connect_timer/test_callback_generation_survives_deferred_runtime_poll.inc'))

    def test_timer_start_failure_disarms_identity_and_keeps_disconnect_protection(self):
        compile_run(self, self.barrier_code() + fixture_text('wifi/station/test_wifi_connect_timer/test_timer_start_failure_disarms_identity_and_keeps_disconnect_protection.inc'))

    def test_disabled_pmf_verifies_config_without_post_start_setter(self):
        compile_run(self, self.barrier_code() + fixture_text('wifi/station/test_wifi_connect_timer/test_disabled_pmf_verifies_config_without_post_start_setter.inc'))
