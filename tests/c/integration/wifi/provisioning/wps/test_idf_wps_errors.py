"""Deferred checks of production WPS SDK init/scan/TX/RSN error paths.

Implementation phase: AST only. Uses the production generator, original SDK
function bodies and native error owner; no independent test state machine.
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
from patch_idf_wps_native import NATIVE_HEADER, NATIVE_SOURCE, patch_source
from patch_idf_wps import function


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class WPSErrors(unittest.TestCase):
    def test_production_init_scan_and_transmit_failures(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(sdk) / 'components/wpa_supplicant'
        relative = 'esp_supplicant/src/esp_wps.c'
        patched = patch_source(relative, (component / relative).read_bytes()).decode()
        native = NATIVE_SOURCE.read_text()
        record = native[native.index('typedef struct {'):native.index('} esp32qjs_wps_native_t;') + len('} esp32qjs_wps_native_t;')]
        code = PRELUDE + declarations((component / 'esp_supplicant/include/esp_wps.h').read_text())
        code += declarations(NATIVE_HEADER.read_text()) + record + BOUNDARIES
        for name in ('esp32qjs_wps_native_scrub','esp32qjs_wps_record_error','esp32qjs_wps_native_pin'):
            code += function(native, name)
        for name in ('wps_build_ic_appie_wps_pr','wps_build_ic_appie_wps_ar','wifi_station_wps_init',
                     'wifi_station_wps_deinit','wifi_wps_scan_done_body','is_ap_supports_sae'):
            code += function(patched, name)
        first = patched.index('static inline int wps_sm_ether_send(')
        last = patched.index('static inline u8 *wps_sm_alloc_eapol(', first)
        code += patched[first:last] + function(patched, 'wps_send_eapol_frame')
        compile_run(self, code + IMPLEMENTATIONS + MAIN)


PRELUDE = fixture_text('wifi/provisioning/wps/test_idf_wps_errors/prelude.inc')

BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_errors/boundaries.inc')

IMPLEMENTATIONS = fixture_text('wifi/provisioning/wps/test_idf_wps_errors/implementations.inc')

MAIN = fixture_text('wifi/provisioning/wps/test_idf_wps_errors/main.inc')
