"""Deferred production registry + RX dispatch tests with controllable native threads."""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import tests.c.integration.wifi.monitor.test_wifi_rx_target as rx_target


class WiFiPromiscuousBroker(unittest.TestCase):
    def test_retained_dispatch_detach_identity_and_requirement_union(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = rx_target.WiFiRxTarget().production_code(profile) + THREADS
                for name in ['wifi_rx_filter', 'wifi_promiscuous_broker']:
                    code += rx_target.unit(rx_target.INTERNAL / ('esp32_mquickjs_' + name + '.h'))
                    code += rx_target.unit(rx_target.COMMON / ('esp32_mquickjs_' + name + '.c'))
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


THREADS = fixture_text('wifi/monitor/test_wifi_promiscuous_broker/threads.inc')

MAIN = fixture_text('wifi/monitor/test_wifi_promiscuous_broker/main.inc')
