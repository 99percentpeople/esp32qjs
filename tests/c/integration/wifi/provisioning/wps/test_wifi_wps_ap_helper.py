"""Deferred production AP helper reservation; AST only this wave."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import unittest
from pathlib import Path
from tests.support.native_compile import compile_run
from tests.support.c_source import extract as function

ROOT = TEST_ROOT
WIFI = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'


class WpsAPHelper(unittest.TestCase):
    def test_exact_reservation_preserves_connected_station_and_blocks_competing_helpers(self):
        source = WIFI.read_text()
        code = TYPES
        for name in ('esp32_mquickjs_wifi_connection_reserved_locked', 'wifi_helpers_idle', 'wifi_driver_helpers_ready'):
            code += function(source, name)
        code += (WIFI.parent / 'esp32_mquickjs_wifi_wps_ap_helper.inc').read_text()
        compile_run(self, code + fixture_text('wifi/provisioning/wps/test_wifi_wps_ap_helper/test_exact_reservation_preserves_connected_station_and_blocks_competing_helpers.inc'))


TYPES = fixture_text('wifi/provisioning/wps/test_wifi_wps_ap_helper/types.inc')
