"""Deferred production Monitor bridge and EventQueue context ownership tests.

JS/SDK resource creation, queue transport and wake scheduling are boundaries.
Constructor, context binding, native lifetime and Future finish/destroy are real.
"""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.monitor.test_wifi_monitor_resources import production_code
import tests.c.integration.wifi.monitor.test_wifi_rx_target as rx_target
from tests.support.c_source import extract as function


def production_queue_code(profile):
    source = (rx_target.ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs_event_queue.c').read_text()
    start = source.index('struct esp32_mquickjs_event_queue {')
    queue_struct = source[start:source.index('\n};', start) + 4]
    code = production_code(profile) + JS_BOUNDARY
    code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_event_queue.h')
    code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_event_queue_resources.h')
    code += queue_struct + QUEUE_BOUNDARY
    for name in ['event_queue_resource_release', 'event_queue_new', 'esp32_mquickjs_event_queue_new_wireless', 'event_queue_destroy_native', 'event_queue_take_destroy_ownership_locked',
                 'esp32_mquickjs_event_queue_bind_context_release', 'esp32_mquickjs_event_queue_retain',
                 'esp32_mquickjs_event_queue_release', 'esp32_mquickjs_event_queue_close',
                 'esp32_mquickjs_event_queue_dispose', 'esp32_mquickjs_event_queue_new',
                 'event_queue_future_finish', 'event_queue_future_destroy']:
        code += function(source, name)
    code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_monitor_queue.h')
    return code + rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                                 'esp32_mquickjs_wifi_monitor_queue.c')


class WiFiMonitorQueue(unittest.TestCase):
    def test_context_outlives_dispose_and_conversion_failure_does_not_leak_root(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = production_queue_code(profile)
                path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                path.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


JS_BOUNDARY = fixture_text('wifi/monitor/test_wifi_monitor_queue/js_boundary.inc')

QUEUE_BOUNDARY = fixture_text('wifi/monitor/test_wifi_monitor_queue/queue_boundary.inc')

MAIN = fixture_text('wifi/monitor/test_wifi_monitor_queue/main.inc')
