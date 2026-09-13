"""Call the production public lifecycle adapters and idle-child admission helper."""
from tests.support.fixtures import fixture_text
import unittest
import tests.c.integration.wifi.station.test_wifi_scan_lifecycle as fixture
from tests.support.native_compile import compile_run

class WiFiPublicLifecycle(unittest.TestCase):
    def code(self):
        code=fixture.WiFiScanLifecycle().code()
        # Replace the base fixture's single-Station boundary definitions with
        # declarations; this fixture supplies the coordinated AP boundaries.
        for name in ['esp32_mquickjs_wifi_ap_control_lease', 'wifi_begin_configuration_cleanup',
                     'wifi_finish_configuration_cleanup']:
            import re
            code=re.sub(r'(static [^\n{};]*\b'+name+r'\([^\n]*?\)) \{[^\n]*\}',r'\1;',code,count=1)

        code=code.replace('static void esp32_mquickjs_wifi_throw_operation_error', 'static int esp32_mquickjs_wifi_throw_operation_error')
        code=code.replace('(void)reason;(void)status; }', '(void)reason;(void)status;return -99; }')
        code += fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/code.inc')
        # Resource cleanup is separately tested at every production SDK boundary.
        code=code.replace('static int wifi_cleanup_failed_init(void) { cleanup_calls++;', 'static int wifi_cleanup_failed_init(void) { cleanup_calls++;')
        source=fixture.WIFI.read_text()
        code+=''.join(fixture.extract(source,n) for n in ['wifi_helpers_idle','wifi_stop_idle','wifi_start_existing_running','js_wifi_start','js_wifi_stop'])
        return code

    def test_stop_routes_pending_configuration_to_central_cleanup(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_stop_routes_pending_configuration_to_central_cleanup.inc'))

    def test_stop_scope_ends_after_failure_and_capture_precedes_admission(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_stop_scope_ends_after_failure_and_capture_precedes_admission.inc'))

    def test_arguments_rejected_before_init_or_driver_effects(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_arguments_rejected_before_init_or_driver_effects.inc'))

    def test_start_is_idempotent_and_stop_releases_only_after_admission(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_start_is_idempotent_and_stop_releases_only_after_admission.inc'))

    def test_stop_never_cancels_connected_or_pending_children(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_stop_never_cancels_connected_or_pending_children.inc'))

    def test_cleanup_failure_retains_exclusion_and_retry_skips_admission(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_cleanup_failure_retains_exclusion_and_retry_skips_admission.inc'))

    def test_healthy_ap_and_apsta_stop_use_central_retirement(self):
        compile_run(self,self.code()+fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_healthy_ap_and_apsta_stop_use_central_retirement.inc'))

    def test_partial_ap_stop_teardown_waits_for_station_terminal_and_fence(self):
        import re
        code=self.code()
        code=re.sub(r'(static int wifi_adopt_ap_stop_cleanup\(void\)) \{[^\n]*\}',r'\1;',code,count=1)
        code += fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_partial_ap_stop_teardown_waits_for_station_terminal_and_fence.inc')
        code += fixture.extract(fixture.WIFI.read_text(),'wifi_adopt_ap_stop_cleanup')
        compile_run(self,code+fixture_text('wifi/lifecycle/test_wifi_public_lifecycle/test_partial_ap_stop_teardown_waits_for_station_terminal_and_fence-02.inc'))
