"""Deferred production Session, capture, queue ownership and real reaper registry.

JS/SDK/task notification/Radio are explicit boundaries; no alternate Session
state machine. The fixture does not establish movable-GC or full runtime proof.
"""
from tests.support.fixtures import fixture_text
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import tests.c.integration.wifi.monitor.test_wifi_rx_target as rx_target
from tests.c.integration.wifi.monitor.test_wifi_monitor_queue import production_queue_code
from tests.c.integration.wifi.monitor.test_wifi_monitor_capture import BOUNDARY_TYPES
from tests.support.c_source import extract as function


class WiFiMonitorSession(unittest.TestCase):
    def test_owners_failure_reaper_pressure_retirement_and_teardown_isolation(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = production_queue_code(profile)
                code += rx_target.unit(rx_target.COMMON / 'esp32_mquickjs_wifi_rx_filter.c')
                code += BOUNDARY_TYPES.split('typedef struct { bool closed,has_event;')[0]
                code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_monitor_capture.h')
                code += RADIO_BOUNDARY
                code += rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                                       'esp32_mquickjs_wifi_monitor_capture.c')
                code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_reaper.h')
                code += rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs_reaper.c')
                code += RUNTIME_BOUNDARY
                code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_monitor_session.h')
                code += rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                                       'esp32_mquickjs_wifi_monitor_session.c')
                public = (rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                          'esp32_mquickjs_wifi_monitor.c').read_text()
                code += rx_target.unit(rx_target.INTERNAL / 'utils/esp32_mquickjs_byte_span.h')
                code += re.search(r'typedef struct \{\n    esp32_mquickjs_wifi_monitor_session_t \*session;\n    esp32_mquickjs_wifi_monitor_ref_t payload;.*?\} monitor_reference_t;',
                                  public, re.S).group(0)
                for name in ['monitor_reference_release_payload', 'monitor_view_release', 'monitor_source_next',
                             'monitor_source_iterator_close', 'monitor_source_open', 'monitor_source_length',
                             'monitor_source_destroy']:
                    code += function(public, name)
                code += re.search(r'typedef struct \{\n    esp32_mquickjs_wifi_monitor_session_t \*session;\n    uint32_t count, capacity;.*?\} monitor_batch_t;',
                                  public, re.S).group(0)
                code += function(public, 'monitor_batch_release')
                code += function(public, 'monitor_batch_adopt_event')
                for name in ['esp32_mquickjs_wifi_rx_wire.h', 'esp32_mquickjs_wifi_csi_layout.h',
                             'esp32_mquickjs_wifi_rx_wire_metadata.h', 'esp32_mquickjs_wifi_monitor_wire.h']:
                    code += rx_target.unit(rx_target.INTERNAL / name)
                for name in ['esp32_mquickjs_wifi_rx_wire.c', 'esp32_mquickjs_wifi_rx_wire_metadata.c']:
                    code += rx_target.unit(rx_target.COMMON / name)
                for name in ['esp32_mquickjs_wifi_rx_vht_signal.h', 'esp32_mquickjs_wifi_rx_he_signal.h']:
                    code += rx_target.unit(rx_target.INTERNAL / name)
                code += rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                                       'esp32_mquickjs_wifi_monitor_wire.c')
                start = public.index('typedef struct {\n    esp32_mquickjs_wifi_monitor_session_t *session;\n    uint8_t *control;')
                end = public.index('static const esp32_mquickjs_byte_span_source_object_ops_t monitor_wire_source_ops', start)
                code += public[start:end]
                path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                path.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_core_requests_monitor_shutdown_before_waiting_for_future_or_reapers(self):
        source = (rx_target.ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs.c').read_text()
        source = source[source.index('static bool esp32_mquickjs_destroy_internal('):]
        self.assertLess(source.index('esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(runtime)'),
                        source.index('esp32_mquickjs_prepare_future_runtime_destroy(ctx, runtime)'))
        self.assertLess(source.index('esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(runtime)'),
                        source.index('esp32_mquickjs_reapers_pending(runtime)'))


RADIO_BOUNDARY = fixture_text('wifi/monitor/test_wifi_monitor_session/radio_boundary.inc')

RUNTIME_BOUNDARY = fixture_text('wifi/monitor/test_wifi_monitor_session/runtime_boundary.inc')

MAIN = fixture_text('wifi/monitor/test_wifi_monitor_session/main.inc')
