"""Deferred real credential/PHY/home-channel recovery checkpoint capture.

Radio recovery admission and saved-PHY SDK calls are injected boundaries with
separate producer fixtures. All capture, validation, secret allocation/zeroing,
immutable checkpoint and failure logic here are production functions.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run


class WiFiRecoveryCheckpoint(unittest.TestCase):
    def test_read_only_complete_phy_home_target_errors_and_secret_retention(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    code = config_code(profile, ap, mutation_boundary=True)
                    code += CONFIG_MAIN[:CONFIG_MAIN.index('int main(void)')]
                    compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/lifecycle/test_wifi_recovery_checkpoint/main.inc')
