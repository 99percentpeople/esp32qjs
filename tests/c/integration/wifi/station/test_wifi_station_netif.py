"""SDK boundary failures in production Station netif prepare/retire helpers."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import unittest
from tests.support.c_source import extract as function
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'


class WiFiStationNetif(unittest.TestCase):
    def test_prepare_failures_unwind_and_failed_detach_is_never_retried(self):
        source = SOURCE.read_text()
        body = function(source, 'wifi_prepare_station_netif') + function(source, 'wifi_retire_station_netif')
        compile_run(self, fixture_text('wifi/station/test_wifi_station_netif/test_prepare_failures_unwind_and_failed_detach_is_never_retried-02.inc') + body + fixture_text('wifi/station/test_wifi_station_netif/test_prepare_failures_unwind_and_failed_detach_is_never_retried.inc'))
