"""Deferred production periodic owner + Session/queue/arbiter/broker coverage.

Only timer, allocator, clock, task queue/locks and Radio/SDK calls are injected.
No JS poller is needed to drive the timer path after initial native admission.
"""
from tests.support.fixtures import fixture_text
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.tx.test_wifi_raw_tx_session import production_session_code, MAIN as SESSION_MAIN, RAW
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit


def production_periodic_job_code():
    code = production_session_code()
    # This adapter adds an outer job mutex; retain allocator/SDK assertions
    # that forbid either mutex during I/O. No Session->job lock is allowed.
    code = code.replace('assert(!critical_depth && !session_mutex_depth);\n    int error=wait',
                        'assert(!critical_depth && session_mutex_depth<2);\n    int error=wait')
    code = code.replace('assert(session_mutex_depth==1 && !critical_depth);--session_mutex_depth;',
                        'assert(session_mutex_depth>=1 && session_mutex_depth<=2 && !critical_depth);--session_mutex_depth;')
    for name in ['periodic', 'periodic_job']:
        code += unit(INTERNAL / ('esp32_mquickjs_wifi_raw_tx_' + name + '.h'))
    code += TIMERS
    for name in ['periodic', 'periodic_job']:
        code += unit(RAW / ('esp32_mquickjs_wifi_raw_tx_' + name + '.c'))
    code += SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
    return code


class WiFiRawTxPeriodicJob(unittest.TestCase):
    def test_autonomous_timer_backpressure_close_and_cleanup_suffix(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        code = production_periodic_job_code()
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / 'fixture.c', Path(directory) / 'fixture'
            source.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


TIMERS = fixture_text('wifi/tx/test_wifi_raw_tx_periodic_job/timers.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_periodic_job/main.inc')
