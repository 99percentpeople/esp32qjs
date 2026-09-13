"""Deferred actual Radio scan checkpoint/replay and hidden intent transfer."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run


class WiFiScanRestart(unittest.TestCase):
    def test_actual_capture_replay_and_hidden_activation(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            code = config_code(profile, True, mutation_boundary=True)
            code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
            with self.subTest(profile=profile):
                compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/station/test_wifi_scan_restart/main.inc')
