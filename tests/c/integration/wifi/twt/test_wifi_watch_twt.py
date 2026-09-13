"""Deferred actual TWT capture/converter; no simulated TWT operation lifecycle."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import build, run
from tests.c.integration.wifi.config.test_wifi_watch_values import watch_code

MAIN = fixture_text('wifi/twt/test_wifi_watch_twt/main.inc')


class WiFiWatchTwt(unittest.TestCase):
    def test_all_twt_value_paths_exact_u64_and_moving_gc(self):
        with tempfile.TemporaryDirectory() as tmp:
            run([str(build(tmp, watch_code(), MAIN))])
