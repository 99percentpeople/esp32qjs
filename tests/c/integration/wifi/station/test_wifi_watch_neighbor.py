"""Deferred production neighbor parser/pool/capture/fanout/VM conversion checks.

Only SDK input, mutex, allocation, FIFO and VM boundaries are injected. These
fixtures are not a roaming operation or an alternative lifetime state machine.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import build, run
from tests.c.integration.wifi.config.test_wifi_watch_values import watch_code

MAIN = fixture_text('wifi/station/test_wifi_watch_neighbor/main.inc')


class WiFiWatchNeighbor(unittest.TestCase):
    def test_bounded_capture_fanout_retirement_and_vm_conversion(self):
        with tempfile.TemporaryDirectory() as tmp:
            run([str(build(tmp, watch_code(), MAIN))])
