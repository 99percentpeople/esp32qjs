"""Deferred production registrar timer identity/admission regressions.

AST only in the implementation wave. Execute with the concentrated Wi-Fi suite;
timer queue boundaries are controlled, production registry/arm/cancel/dispatch
and SDK PBC/PIN admission bodies remain the code under test.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function
from patch_idf_wps_registrar import patch_source


class WpsRegistrarTimers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to reviewed ESP-IDF')
        root = Path(sdk) / 'components/wpa_supplicant'
        cls.original = (root / 'src/wps/wps_registrar.c').read_bytes()
        cls.ap_original = (root / 'esp_supplicant/src/esp_hostpad_wps.c').read_bytes()
        cls.patched = patch_source('src/wps/wps_registrar.c', cls.original).decode()
        start = cls.patched.index('static struct wps_registrar *esp32qjs_wps_registrars;')
        end = cls.patched.index(function(cls.patched, 'esp32qjs_wps_registrar_timer_detach'))
        end += len(function(cls.patched, 'esp32qjs_wps_registrar_timer_detach'))
        cls.timers = cls.patched[start:end]

    def test_cancel_rearm_destroy_reuse_and_ticket_exhaustion(self):
        compile_run(self, TYPES + self.timers + fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_timers/test_cancel_rearm_destroy_reuse_and_ticket_exhaustion.inc'))

    def test_registration_failure_preserves_old_timer_and_consumes_identity(self):
        compile_run(self, TYPES + self.timers + fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_timers/test_registration_failure_preserves_old_timer_and_consumes_identity.inc'))

    def test_original_pbc_ignores_timer_failure_patched_preserves_state(self):
        for patched in (False, True):
            src = self.patched if patched else self.original.decode()
            code = TYPES + (self.timers if patched else '') + PBC_BOUNDARIES
            code += function(src, 'wps_registrar_button_pushed')
            setup = 'esp32qjs_wps_registrars=&reg;' if patched else ''
            code += r'''
int main(void) {
 struct wps_registrar reg={.force_pbc_overlap=9};
''' + setup + r'''
 register_error=-73;int ret=wps_registrar_button_pushed(&reg,NULL);
''' + (fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_timers/test_original_pbc_ignores_timer_failure_patched_preserves_state.inc') if patched else r'''
 assert(ret==0 && reg.pbc && reg.selected_registrar);
 assert(changed==1 && active_events==1 && authorized==1);
''') + 'return 0;}\n'
            with self.subTest(patched=patched): compile_run(self, code)

    def test_pin_timer_failure_frees_new_secret_before_publication(self):
        code = TYPES + self.timers + PBC_BOUNDARIES + PIN_BOUNDARIES
        code += function(self.patched, 'wps_registrar_add_pin')
        compile_run(self, code + fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_timers/test_pin_timer_failure_frees_new_secret_before_publication.inc'))

    def test_start_propagates_mode_status_and_timer_errors(self):
        for patched in (False, True):
            source = patch_source('esp_supplicant/src/esp_hostpad_wps.c', self.ap_original).decode() if patched else self.ap_original.decode()
            code = TYPES + START_BOUNDARIES + function(source, 'wifi_ap_wps_start_internal')
            code += r'''
int main(void){
 mode_error=0x123;int ret=wifi_ap_wps_start_internal(NULL);
''' + ('assert(ret==0x123 && !status_calls);' if patched else 'assert(ret==ESP_ERR_WIFI_MODE);') + r'''
 mode_error=0;status_error=0x456;status_calls=0;ret=wifi_ap_wps_start_internal(NULL);
''' + ('assert(ret==0x456 && status_calls==1);' if patched else 'assert(ret==ESP_FAIL);') + r'''
 status_error=0;status_calls=0;start_error=-73;ret=wifi_ap_wps_start_internal(NULL);
''' + ('assert(ret==-73 && status_calls==2 && native_status==WPS_STATUS_DISABLE);' if patched else 'assert(ret==ESP_FAIL);') + r'''
 start_error=0;assert(wifi_ap_wps_start_internal(NULL)==ESP_OK);
 return 0;
}
'''
            if patched: code = code.replace('wifi_ap_wps_start_internal(NULL)', 'wifi_ap_wps_start_internal(NULL, 0)')
            with self.subTest(patched=patched): compile_run(self, code)


TYPES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_timers/types.inc')

PBC_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_timers/pbc_boundaries.inc')

PIN_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_timers/pin_boundaries.inc')

START_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_timers/start_boundaries.inc')
