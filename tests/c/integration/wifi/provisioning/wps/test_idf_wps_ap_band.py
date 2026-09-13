"""Deferred production RF callbacks and M1/M2/M2D builders, SDK/crypto injected.

Only AST-parse during implementation. The original pinned callback reproduces
the wrong 5 GHz RF attribute; patched production readers/builders must refuse
an unavailable band before allocation or crypto. This does not prove RF.
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
from patch_idf_wps import function, patch_source as patch_station
from patch_idf_wps_registrar import patch_source


class WpsAPBand(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        root = Path(sdk) / 'components/wpa_supplicant'
        cls.original = (root / 'src/ap/wps_hostapd.c').read_bytes()
        cls.host = patch_source('src/ap/wps_hostapd.c', cls.original).decode()
        cls.registrar = patch_source('src/wps/wps_registrar.c',
                                     (root / 'src/wps/wps_registrar.c').read_bytes()).decode()
        cls.original_station = (root / 'esp_supplicant/src/esp_wps.c').read_bytes()
        cls.station = patch_station('esp_supplicant/src/esp_wps.c', cls.original_station).decode()
        cls.enrollee = patch_station('src/wps/wps_enrollee.c',
                                    (root / 'src/wps/wps_enrollee.c').read_bytes()).decode()

    def test_original_ap_callback_misreports_five_ghz(self):
        compile_run(self, TYPES + function(self.original.decode(), 'hostapd_wps_rf_band_cb') + r'''
int main(void) {
    band=WIFI_BAND_5G;
    assert(hostapd_wps_rf_band_cb(NULL)==WPS_RF_24GHZ && !reads);
    return 0;
}
''')

    def test_patched_callback_tracks_live_band_and_preserves_read_error(self):
        code = ''.join(function(self.host, name) for name in (
            'esp32qjs_wps_ap_rf_band_read', 'hostapd_wps_rf_band_cb'))
        for dualband in (0, 1):
            with self.subTest(dualband=dualband):
                compile_run(self, f'#define CONFIG_SOC_WIFI_SUPPORT_5G {dualband}\n' + TYPES + code + fixture_text('wifi/provisioning/wps/test_idf_wps_ap_band/test_patched_callback_tracks_live_band_and_preserves_read_error.inc'))

    def test_original_station_callback_masks_read_failure(self):
        compile_run(self, TYPES + function(self.original_station.decode(), 'wps_rf_band_cb') + fixture_text('wifi/provisioning/wps/test_idf_wps_ap_band/test_original_station_callback_masks_read_failure.inc'))

    def test_station_callback_rejects_failed_unknown_and_unsupported_band_modes(self):
        code = function(self.station, 'wps_rf_band_cb')
        for dualband in (0, 1):
            with self.subTest(dualband=dualband):
                compile_run(self, f'#define CONFIG_SOC_WIFI_SUPPORT_5G {dualband}\n' + TYPES + code + fixture_text('wifi/provisioning/wps/test_idf_wps_ap_band/test_station_callback_rejects_failed_unknown_and_unsupported_band_modes.inc'))

    def test_builders_use_single_band_read_and_reject_before_allocation(self):
        code = function(self.enrollee, 'wps_build_m1')
        code += ''.join(function(self.registrar, name) for name in ('wps_build_m2', 'wps_build_m2d'))
        compile_run(self, TYPES + BUILDERS + code + fixture_text('wifi/provisioning/wps/test_idf_wps_ap_band/test_builders_use_single_band_read_and_reject_before_allocation.inc'))


TYPES = fixture_text('wifi/provisioning/wps/test_idf_wps_ap_band/types.inc')


BUILDERS = fixture_text('wifi/provisioning/wps/test_idf_wps_ap_band/builders.inc')
