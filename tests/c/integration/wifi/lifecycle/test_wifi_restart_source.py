"""Deferred real source START/capture; SDK and physical STOP boundaries injected.

Exercises production checkpoint code, mutation aliases, START helper, RF reads
and secret retirement. This is not an SDK scheduler, NVS or hardware test.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run


class WiFiRestartSource(unittest.TestCase):
    def test_healthy_stopped_sources_ram_capture_normalization_and_each_sdk_failure(self):
        setup = CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    code = config_code(profile, ap, mutation_boundary=True)
                    original = 'native_running=true;++native_starts;return sdk_step(true);'
                    replacement = ('native_config[0].sta.listen_interval=9;'
                                   'native_country.max_tx_power=18;native_tx_power=60;' + original)
                    self.assertEqual(code.count(original), 1)
                    code = code.replace(original, replacement)
                    compile_run(self, code + setup + MAIN)


MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_source/main.inc')
