"""Deferred real Monitor capture/resources with explicit Radio/queue boundaries."""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import tests.c.integration.wifi.monitor.test_wifi_rx_target as rx_target
from tests.c.integration.wifi.monitor.test_wifi_monitor_resources import production_code


class WiFiMonitorCapture(unittest.TestCase):
    def test_start_failure_drain_retry_fixed_conflict_and_close(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = production_code(profile)
                code += rx_target.unit(rx_target.COMMON / 'esp32_mquickjs_wifi_rx_filter.c')
                code += BOUNDARY_TYPES
                code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_monitor_capture.h')
                code += BOUNDARIES
                code += rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                                       'esp32_mquickjs_wifi_monitor_capture.c')
                path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                path.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARY_TYPES = fixture_text('wifi/monitor/test_wifi_monitor_capture/boundary_types.inc')

BOUNDARIES = fixture_text('wifi/monitor/test_wifi_monitor_capture/boundaries.inc')

MAIN = fixture_text('wifi/monitor/test_wifi_monitor_capture/main.inc')
