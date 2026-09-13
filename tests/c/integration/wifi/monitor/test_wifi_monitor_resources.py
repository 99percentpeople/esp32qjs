"""Deferred tests of production Monitor ownership, with allocator/queue boundaries.

The pthread pause models scheduling at publication, not an alternative pool.
Recorded target types do not establish ESP32 ABI or RF evidence.
"""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import tests.c.integration.wifi.monitor.test_wifi_rx_target as rx_target
from tests.c.integration.wifi.monitor.test_wifi_promiscuous_broker import THREADS


def production_code(profile):
    code = rx_target.WiFiRxTarget().production_code(profile) + THREADS + PRELUDE
    for name in ['native_pool', 'native_lease', 'wifi_rx_filter', 'wifi_monitor_resources']:
        code += rx_target.unit(rx_target.INTERNAL / ('esp32_mquickjs_' + name + '.h'))
    for name in ['native_pool', 'native_lease']:
        code += rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/core' /
                               ('esp32_mquickjs_' + name + '.c'))
    return code + rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                                 'esp32_mquickjs_wifi_monitor_resources.c')


class WiFiMonitorResources(unittest.TestCase):
    def test_allocation_publication_retention_reuse_and_close_races(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = production_code(profile)
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


PRELUDE = r'''
#include <stdlib.h>
#include <limits.h>
#include <stdatomic.h>
#define portMUX_INITIALIZE(lock) do { assert(!pthread_mutex_init(lock,NULL)); } while(0)
'''

MAIN = fixture_text('wifi/monitor/test_wifi_monitor_resources/main.inc')
