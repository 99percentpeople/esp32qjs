"""Deferred registrar credential and initialization failure regressions.

Executes fixed-SDK production function bodies with allocator/driver/EAP boundaries.
Original branches document ignored errors/borrowed-context free before correction.
Not RF, callback retirement or managed registrar Session proof. AST only during
implementation; execute with the concentrated Wi-Fi suite.
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
from patch_idf_wps_registrar import OUTPUTS, patch_source


class WpsRegistrarInit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        cls.component = Path(sdk) / 'components/wpa_supplicant'
        cls.original = {p: (cls.component / p).read_bytes() for p in OUTPUTS.values()}
        cls.patched = {p: patch_source(p, s).decode() for p, s in cls.original.items()}

    def test_hash_drift_and_double_patch_reject(self):
        for path, source in self.original.items():
            for invalid in (source + b'\n', self.patched[path].encode()):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    patch_source(path, invalid)

    def test_credential_bounds_security_atomic_replacement_and_secret_clear(self):
        source = self.patched['src/ap/wps_hostapd.c']
        code = TYPES + function(source, 'hostapd_wps_config_ap')
        compile_run(self, code + CREDENTIAL_CASES)

    def test_original_host_init_reports_success_after_eap_failure_and_frees_borrowed_context(self):
        source = self.original['src/ap/wps_hostapd.c'].decode()
        code = TYPES + HOST_BOUNDARIES + function(source, 'hostapd_init_wps')
        compile_run(self, code + ORIGINAL_HOST_CASES)

    def test_host_init_returns_failure_and_preserves_caller_context(self):
        source = self.patched['src/ap/wps_hostapd.c']
        code = TYPES + HOST_BOUNDARIES + function(source, 'hostapd_init_wps')
        compile_run(self, code + PATCHED_HOST_CASES)

    def test_registrar_pointer_failure_keeps_driver_ie_error(self):
        source = self.patched['src/ap/wps_hostapd.c']
        boundary = HOST_BOUNDARIES.replace('if(++step==fail_step)return NULL;registrars++;',
            'if(++step==fail_step){esp32qjs_wps_host_ie_error=-83;return NULL;}registrars++;')
        compile_run(self, TYPES + boundary + function(source, 'hostapd_init_wps') + fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/test_registrar_pointer_failure_keeps_driver_ie_error.inc'))

    def test_update_uses_matching_live_protocol_data_and_does_not_publish_failed_capture(self):
        relative = 'src/ap/wps_hostapd.c'
        for patched in (False, True):
            source = self.patched[relative] if patched else self.original[relative].decode()
            code = TYPES + UPDATE_BOUNDARIES + function(source, 'hostapd_update_wps')
            main = UPDATE_CASES.replace('PATCHED', '1' if patched else '0')
            with self.subTest(patched=patched): compile_run(self, code + main)

    def test_enable_preserves_mode_factory_and_initializer_errors(self):
        relative = 'esp_supplicant/src/esp_hostpad_wps.c'
        for patched in (False, True):
            source = self.patched[relative] if patched else self.original[relative].decode()
            code = TYPES + ENABLE_BOUNDARIES + function(source, 'wifi_ap_wps_enable_internal')
            main = ENABLE_CASES.replace('PATCHED', '1' if patched else '0')
            if patched: main = main.replace('wifi_ap_wps_enable_internal(&config)', 'wifi_ap_wps_enable_internal(&config, 0)')
            with self.subTest(patched=patched): compile_run(self, code + main)

    def test_native_init_retains_allocations_until_checked_deinit(self):
        relative = 'esp_supplicant/src/esp_hostpad_wps.c'
        for patched in (False, True):
            source = self.patched[relative] if patched else self.original[relative].decode()
            code = TYPES + NATIVE_BOUNDARIES + function(source, 'wifi_ap_wps_init')
            if patched: code += function(source, 'wifi_ap_wps_deinit')
            code += '\nint main(void){\n'
            code += 'esp_wps_config_t config={0};injected_host_error=ESP_ERR_NO_MEM;\n'
            code += 'int ret=wifi_ap_wps_init(&config);\n'
            if patched:
                code += 'assert(ret==ESP_ERR_NO_MEM && gWpsSm && !data_freed && !context_freed);\n'
                code += 'assert(wifi_ap_wps_deinit()==ESP_OK && !gWpsSm && data_freed && context_freed);\n'
            else:
                code += 'assert(ret==ESP_OK && gWpsSm && !data_freed && !context_freed);\n'
            code += 'return 0;}\n'
            with self.subTest(patched=patched): compile_run(self, code)


TYPES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/types.inc')

CREDENTIAL_CASES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/credential_cases.inc')

HOST_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/host_boundaries.inc')
ORIGINAL_HOST_CASES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/original_host_cases.inc')
PATCHED_HOST_CASES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/patched_host_cases.inc')
NATIVE_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/native_boundaries.inc')

UPDATE_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/update_boundaries.inc')
UPDATE_CASES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/update_cases.inc')

ENABLE_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/enable_boundaries.inc')
ENABLE_CASES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_init/enable_cases.inc')
