"""Compile production Wi-Fi helpers with SDK boundaries replaced by fixtures."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from tests.support.native_compile import compile_run
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tests.support.wireless_allocator_fixture import allocation_boundary
from tests.support.c_source import extract as function

ROOT = TEST_ROOT
MODULES = ROOT/'components/esp32_mquickjs/src/modules'





class WirelessControlRegression(unittest.TestCase):
    def test_wifi_control_terminal_survives_full_driver_queue(self):
        body = function((MODULES/'wifi/esp32_mquickjs_wifi.c').read_text(), 'wifi_publish_driver_event_from_callback')
        compile_run(self, fixture_text('wireless/test_wireless_control_regression/test_wifi_control_terminal_survives_full_driver_queue-02.inc') + body + fixture_text('wireless/test_wireless_control_regression/test_wifi_control_terminal_survives_full_driver_queue.inc'))

    def test_wifi_owned_password_is_zeroed_before_destroy(self):
        body = function((MODULES/'wifi/esp32_mquickjs_wifi_future.c').read_text(), 'wifi_future_destroy')
        compile_run(self, fixture_text('wireless/test_wireless_control_regression/test_wifi_owned_password_is_zeroed_before_destroy-02.inc') + body + fixture_text('wireless/test_wireless_control_regression/test_wifi_owned_password_is_zeroed_before_destroy.inc'))
