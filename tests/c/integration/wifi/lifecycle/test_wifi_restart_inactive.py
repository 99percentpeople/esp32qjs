"""Deferred production active-interface inactive-time checkpoint and replay.

SDK calls, physical rebuild/start, locks and allocation storage are injected.
These cases do not establish RF continuity, NVS persistence or hidden-interface
history. They call the production STOP observation/capture/replay/acceptance.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run


class WiFiRestartInactive(unittest.TestCase):
    def test_active_interfaces_stopped_history_failures_and_ram_only_restore(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    code = config_code(profile, ap, mutation_boundary=True)
                    code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
                    compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_inactive/main.inc')
