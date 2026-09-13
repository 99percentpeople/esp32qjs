"""Deferred production one-shot worker/poll/destructor and native broker tests.

Radio calls and the bounded background queue are injected boundaries. The actual
Radio implementation is exercised separately by test_wifi_raw_tx_radio.py.
"""
from tests.support.fixtures import fixture_text
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.tx.test_wifi_raw_tx_broker import production_code
from tests.c.integration.wifi.tx.test_wifi_tx_rate import rate_code
from tests.c.integration.wifi.config.test_wifi_config_controls import ROOT, HEADER, structure
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit

SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx.c'


class WiFiRawTxFuture(unittest.TestCase):
    def test_queue_rejection_cancellation_late_completion_and_cleanup_suffix(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = SOURCE.read_text()
        code = production_code('esp32c5/representative')
        code += rate_code('esp32c5/representative', ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t'))
        code = code.replace('static int64_t esp_timer_get_time(void)',
                            'static int64_t fake_now=123456789;\nstatic int64_t esp_timer_get_time(void)')
        code = code.replace('return 123456789;', 'return fake_now;')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_lane.h')
        code += unit(SOURCE.with_name('esp32_mquickjs_wifi_raw_tx_lane.c'))
        code += '#include <stdatomic.h>\n'
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', HEADER.read_text()).group(0)
        code += structure(HEADER.read_text(), 'esp32_mquickjs_wifi_radio_lease_t')
        code += structure(source, 'raw_tx_options_t')
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += source[start:source.index('\n};', start) + 3]
        start = source.index('static portMUX_TYPE s_retired_lock')
        code += source[start:source.index('static bool raw_tx_retired_pending', start)]
        code += BOUNDARIES
        for name in ['raw_tx_retired_pending', 'raw_tx_cleanup_worker', 'raw_tx_service',
                     'esp32_mquickjs_prepare_wifi_raw_tx_runtime_destroy', 'raw_tx_send_worker',
                     'raw_tx_schedule', 'raw_tx_start', 'raw_tx_poll', 'raw_tx_cancel', 'raw_tx_destroy',
                     'raw_tx_timeout', 'raw_tx_resource']:
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as tmp:
            path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            path.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_future/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_future/main.inc')
