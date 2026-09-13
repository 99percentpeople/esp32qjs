"""Deferred real native Session/queue/arbiter/broker worker ownership coverage.

Only allocator, task locks/worker queue, clock and Radio/SDK boundaries are
injected. No replacement Session or FIFO state machine drives these tests.
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
from tests.c.integration.wifi.config.test_wifi_config_controls import ROOT, HEADER, RADIO, structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit
from tests.support.wireless_vm_fixture import extract

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


def production_session_code(identity=False):
    code = production_code('esp32c5/representative', identity=identity)
    code += rate_code('esp32c5/representative', ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t'))
    code = code.replace('static int64_t esp_timer_get_time(void)',
                        'static int64_t fake_now=123456789;\nstatic int64_t esp_timer_get_time(void)')
    code = code.replace('return 123456789;', 'return fake_now;')
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', HEADER.read_text()).group(0)
    code += structure(HEADER.read_text(), 'esp32_mquickjs_wifi_radio_lease_t')
    code += structure(HEADER.read_text(), 'esp32_mquickjs_wifi_radio_lifecycle_t')
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_raw_tx_ap.h').read_text(), 'esp32_mquickjs_wifi_raw_tx_ap_context_t')
    for name in ['lane', 'queue', 'session']:
        code += unit(INTERNAL / ('esp32_mquickjs_wifi_raw_tx_' + name + '.h'))
    code += extract(RADIO.read_text(), "esp32_mquickjs_wifi_radio_5ghz_channel_bit")
    code += BOUNDARIES
    for name in ['lane', 'queue', 'session']:
        code += unit(RAW / ('esp32_mquickjs_wifi_raw_tx_' + name + '.c'))
    code += fixture_text('wifi/tx/test_wifi_raw_tx_session/broker_status.inc')
    return code


class WiFiRawTxSession(unittest.TestCase):
    def test_shared_grant_batch_flush_late_completion_close_and_cleanup_failure(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        code = production_session_code()
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            source.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_window_refill_out_of_order_fence_and_close(self):
        from tests.support.native_compile import compile_run
        code = production_session_code(identity=True)
        code += MAIN[:MAIN.index('int main(void)')]
        compile_run(self, code + fixture_text('wifi/tx/test_wifi_raw_tx_session/window.inc'))


BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_session/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_session/main.inc')
