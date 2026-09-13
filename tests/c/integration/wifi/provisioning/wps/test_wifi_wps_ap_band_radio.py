"""Deferred production AP Radio admission; worker and regulatory boundaries injected."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract

ROOT = TEST_ROOT
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_wps_ap_radio.inc'


class WpsAPBandRadio(unittest.TestCase):
    def test_regulatory_five_ghz_admission_and_rejected_channels_do_not_start_native(self):
        compile_run(self, TYPES + extract(SOURCE.read_text(), 'esp32_mquickjs_wifi_radio_wps_ap_begin') + MAIN)


TYPES = fixture_text('wifi/provisioning/wps/test_wifi_wps_ap_band_radio/types.inc')


MAIN = fixture_text('wifi/provisioning/wps/test_wifi_wps_ap_band_radio/main.inc')
