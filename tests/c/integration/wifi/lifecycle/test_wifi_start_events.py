"""A cached Station START cannot bypass production Radio admission/fault checks."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import unittest
from tests.support.wireless_vm_fixture import extract
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT


class WiFiStartEvents(unittest.TestCase):
    def test_cached_started_still_checks_exact_radio_owner_and_native_result(self):
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        compile_run(self, fixture_text('wifi/lifecycle/test_wifi_start_events/test_cached_started_still_checks_exact_radio_owner_and_native_result-02.inc') + extract(source, 'esp32_mquickjs_wifi_ensure_started') + fixture_text('wifi/lifecycle/test_wifi_start_events/test_cached_started_still_checks_exact_radio_owner_and_native_result.inc'))
