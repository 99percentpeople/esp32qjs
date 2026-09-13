"""Controlled default-loop scheduling against the entire production retire unit.

No replacement retirement state machine: only SDK queue/netif/RTOS boundaries
are simulated. Execution is deferred to the Wi-Fi API phase test run.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_netif.c'


class WiFiNetifRetirement(unittest.TestCase):
    def code(self, wait_source=None):
        source = '\n'.join(line for line in SOURCE.read_text().splitlines()
                           if not line.startswith('#include '))
        boundary = fixture_text('wifi/station/test_wifi_netif_retirement/code-boundary.inc')
        if wait_source is None:
            wait_source = 'static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { return fallback; }\n'
        return boundary + wait_source + source

    def test_queue_timeout_late_delivery_suffix_and_detach_poison(self):
        compile_run(self, self.code() + fixture_text('wifi/station/test_wifi_netif_retirement/test_queue_timeout_late_delivery_suffix_and_detach_poison.inc'))
