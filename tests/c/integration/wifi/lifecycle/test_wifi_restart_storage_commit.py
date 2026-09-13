"""Deferred actual restart replay/acceptance and final storage commit ordering.

The SDK START boundary records whether an AP's implicit configuration save would
run in FLASH. This is an ordering regression, not a substitute for NVS/RF tests.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run

MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_storage_commit/main.inc')


class WiFiRestartStorageCommit(unittest.TestCase):
    def test_ram_through_ap_start_and_verification_then_commit_before_owners(self):
        helpers = CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, config_code(profile, ap) + helpers + MAIN)
