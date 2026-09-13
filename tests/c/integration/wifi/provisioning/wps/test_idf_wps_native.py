"""Deferred production WPS result/timer boundary checks.

Only AST-check during implementation. Does not prove SDK scan/TX retirement,
Radio admission, native IPC, public Futures, or RF negotiation.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import re
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps_native import NATIVE_HEADER, NATIVE_SOURCE, AP_RESULT_HEADER, patch_source
from patch_idf_wps import function


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class IDFWPSNative(unittest.TestCase):
    def test_production_result_identity_and_secret_lifetime(self):
        self.production_case(MAIN)

    def test_ap_result_blocks_station_before_native_allocation_or_mutation(self):
        self.production_case(fixture_text('wifi/provisioning/wps/test_idf_wps_native/test_ap_result_blocks_station_before_native_allocation_or_mutation.inc'), registrar=True)

    def production_case(self, main, registrar=False):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(sdk) / 'components/wpa_supplicant'
        relative = 'esp_supplicant/src/esp_wps.c'
        patched = patch_source(relative, (component / relative).read_bytes()).decode()
        wps_header = (component / 'src/wps/wps.h').read_text()
        start = wps_header.index('struct wps_credential {')
        cred = wps_header[start:wps_header.index('\n};', start) + 3]
        os_header = (component / 'port/include/os.h').read_text()
        start = os_header.index('static void * (* const volatile memset_func)')
        zero = os_header[start:os_header.index('\n#endif', start)]
        native = declarations(NATIVE_SOURCE.read_text())
        # The target ABI is checked in real C3/S3/C5 builds. Host pointer width
        # differs; the production high/low helpers still carry 32-bit words.
        native = native.replace('_Static_assert(sizeof(uintptr_t) == 4, "review WPS timer identity encoding");', '')
        code = PRELUDE
        if registrar:
            code += '\n#define CONFIG_WPS_REGISTRAR 1\nstatic bool ap_reserved;\nbool esp32qjs_wps_ap_result_held(void){return ap_reserved;}\n'
        code += declarations((component / 'esp_supplicant/include/esp_wps.h').read_text())
        if registrar:
            code += declarations(AP_RESULT_HEADER.read_text())
            code += '\nint esp32qjs_wps_ap_result_status(uint32_t id, esp32_mquickjs_wifi_wps_ap_result_status_t *out){(void)id;(void)out;return ESP_ERR_INVALID_STATE;}\n'
        code += declarations(NATIVE_HEADER.read_text()) + '\n' + cred + '\n' + BOUNDARIES
        code += zero + native + function(patched, 'esp32qjs_wps_credential_valid')
        code += function(patched, 'wps_send_event_and_disable')
        code += function(patched, 'wifi_station_wps_timeout_body')
        code += function(patched, 'wifi_station_wps_timeout')
        compile_run(self, code + IMPLEMENTATIONS + main)


PRELUDE = fixture_text('wifi/provisioning/wps/test_idf_wps_native/prelude.inc')

BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_native/boundaries.inc')

IMPLEMENTATIONS = fixture_text('wifi/provisioning/wps/test_idf_wps_native/implementations.inc')

MAIN = fixture_text('wifi/provisioning/wps/test_idf_wps_native/main.inc')
