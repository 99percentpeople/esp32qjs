"""Deferred production stopped capture/replay with real mutation aliases."""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run


class WiFiRestartStopped(unittest.TestCase):
    def test_real_stopped_observation_admission_capture_oom_and_hidden_band(self):
        helpers = CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, config_code(profile, ap, mutation_boundary=True) + helpers + MAIN)


MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_stopped/main.inc')
