"""Deferred production hidden-value handoff and activation restoration.

SDK/physical driver storage and locks are injected. Explicit fault clearing in
one case isolates the retained readback suffix; it is not public recovery proof.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run


class WiFiInactiveDeferred(unittest.TestCase):
    def test_frozen_handoff_activation_no_duplicate_write_and_next_restart(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                code = config_code(profile, True, mutation_boundary=True)
                code = code.replace('assert(mode==WIFI_MODE_STA &&',
                    'assert((mode==WIFI_MODE_STA || mode==WIFI_MODE_AP || mode==WIFI_MODE_APSTA) &&')
                code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
                compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/config/test_wifi_inactive_deferred/main.inc')
