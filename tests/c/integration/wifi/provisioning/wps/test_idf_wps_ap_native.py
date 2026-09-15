"""Deferred whole production AP result/native lifecycle; AST only this wave.

SDK enable/start/destructor and timer/driver boundaries are controlled here.
The lifecycle, command authorization, error retention and release are the
production includes, not a separate test state machine. Real worker/RTOS and
full SDK input scheduling remain part of the concentrated Wi-Fi tests.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import os
import sys
import unittest
from tests.support.native_compile import compile_run
from tests.c.integration.wifi.provisioning.wps.test_idf_wps_ap_result import PRELUDE, POST, HEADER, SOURCE, declarations

ROOT = TEST_ROOT
NATIVE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_sdk.inc'
NATIVE_HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_ap_sdk.h'
sys.path.insert(0, str(ROOT / 'scripts'))
from sdk_patches.wpa.wps.credentials import function
from sdk_patches.wpa.wps.registrar import patch_source


class WpsAPNative(unittest.TestCase):
    def run_case(self, main):
        compile_run(self, PRELUDE + TYPES + declarations(HEADER.read_text()) +
                    declarations(NATIVE_HEADER.read_text()) + declarations(SOURCE.read_text()) +
                    POST + BOUNDARIES + declarations(NATIVE.read_text()) + main)

    def test_ordinary_ap_and_station_disable_are_rejected_before_mutation(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        relative = 'esp_supplicant/src/esp_hostpad_wps.c'
        source = patch_source(relative, (Path(sdk) / 'components/wpa_supplicant' / relative).read_bytes()).decode()
        station = (NATIVE.parent / 'esp32_mquickjs_wifi_wps_sdk.inc').read_text()
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_native/test_ordinary_ap_and_station_disable_are_rejected_before_mutation-02.inc') + function(source, 'wifi_ap_wps_disable_internal') +
            function(station, 'esp32qjs_wps_unmanaged_disable') + fixture_text('wifi/provisioning/wps/test_idf_wps_ap_native/test_ordinary_ap_and_station_disable_are_rejected_before_mutation.inc'))

    def test_managed_commands_callback_depth_retirement_and_exact_release(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_native/test_managed_commands_callback_depth_retirement_and_exact_release.inc'))

    def test_enable_failure_before_bind_and_factory_cleanup_suffix(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_native/test_enable_failure_before_bind_and_factory_cleanup_suffix.inc'))

    def test_pin_commit_and_terminal_metadata_survive_heap_retirement(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_native/test_pin_commit_and_terminal_metadata_survive_heap_retirement.inc'))


TYPES = fixture_text('wifi/provisioning/wps/test_idf_wps_ap_native/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_ap_native/boundaries.inc')
