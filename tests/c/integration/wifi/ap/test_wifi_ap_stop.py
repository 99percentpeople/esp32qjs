"""Production APSTA stopAP coordinator; test execution deferred to Wi-Fi phase.

Radio and netif boundaries are injected here. Their full production translation
units have separate SDK/scheduler fixtures. No alternative coordinator model.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
import tests.c.integration.wifi.config.test_wifi_configuration_cleanup as fixture
from tests.support.native_compile import compile_run


class WiFiAPStop(unittest.TestCase):
    def code(self):
        code = fixture.WiFiConfigurationCleanup().code()
        # Use the real shared cleanup exports instead of the independent-AP stubs.
        for name in ['esp32_mquickjs_wifi_ap_stop_pending', 'esp32_mquickjs_wifi_stop_ap_shared']:
            code = re.sub(r'static ([^\n{};]*\b' + name + r'\([^\n]*?\)) \{[^\n]*\}',
                          r'\1;', code, count=1)
        code = code.replace('assert(native_token.identity && !running);',
                            'assert(native_token.identity && (!running || partial_quiesced));')
        code = code.replace('static bool expect_stop_only;', 'static bool expect_stop_only,partial_quiesced;')
        # The native retire helper may time out without releasing its storage.
        code = code.replace('ap_calls++;if(fail_at==2)return -2;',
                            'ap_calls++;if(fail_at==2)return -2;')
        code += BOUNDARIES
        ap, wifi = fixture.AP.read_text(), fixture.WIFI.read_text()
        code += ''.join(fixture.function(ap, n) for n in [
            'esp32_mquickjs_wifi_ap_control_lease', 'wifi_ap_partial_token',
            'esp32_mquickjs_wifi_ap_begin_partial_stop', 'esp32_mquickjs_wifi_ap_retire_partial_stop',
            'esp32_mquickjs_wifi_ap_finish_partial_stop', 'esp32_mquickjs_wifi_ap_adopt_partial_stop'])
        code += ''.join(fixture.function(wifi, n) for n in [
            'esp32_mquickjs_wifi_ap_stop_pending', 'esp32_mquickjs_wifi_stop_ap_shared',
            'wifi_adopt_ap_stop_cleanup', 'wifi_stop_idle'])
        code += fixture.function(ap, 'js_wifi_stop_ap')
        return code + SETUP

    def test_stop_ap_preserves_station_and_retries_only_unfinished_resources(self):
        compile_run(self, self.code() + fixture_text('wifi/ap/test_wifi_ap_stop/test_stop_ap_preserves_station_and_retries_only_unfinished_resources.inc'))

    def test_busy_and_foreign_admission_does_not_poison_the_ap(self):
        compile_run(self, self.code() + fixture_text('wifi/ap/test_wifi_ap_stop/test_busy_and_foreign_admission_does_not_poison_the_ap.inc'))

    def test_failed_partial_close_hands_same_token_to_whole_stop_after_station_exits(self):
        compile_run(self, self.code() + fixture_text('wifi/ap/test_wifi_ap_stop/test_failed_partial_close_hands_same_token_to_whole_stop_after_station_exits.inc'))

    def test_stale_tokens_and_permanent_detach_failure_keep_storage(self):
        compile_run(self, self.code() + fixture_text('wifi/ap/test_wifi_ap_stop/test_stale_tokens_and_permanent_detach_failure_keep_storage.inc'))


BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_stop/boundaries.inc')

SETUP = fixture_text('wifi/ap/test_wifi_ap_stop/setup.inc')
