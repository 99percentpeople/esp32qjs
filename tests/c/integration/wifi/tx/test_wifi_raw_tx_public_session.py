"""Deferred public Future control over the real native Raw TX Session stack.

The generic Future scheduler and JS conversion are outside this C fixture;
start/poll/cancel/destroy and native ownership are the production functions.
"""
from tests.support.fixtures import fixture_text
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.tx.test_wifi_raw_tx_session import production_session_code, MAIN as SESSION_MAIN, RAW
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import extract, INTERNAL


class WiFiRawTxPublicSession(unittest.TestCase):
    def test_future_cancel_record_retirement_flush_and_closed_handle_detach(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (RAW / 'esp32_mquickjs_wifi_raw_tx_public_session.c').read_text()
        future = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
        code = production_session_code()
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t;\n'
        code += structure(future, 'esp32_mquickjs_future_token_t')
        for name in ['esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t']:
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('typedef esp32_mquickjs_wifi_raw_tx_session_t native_session_t;')
        code += source[start:source.index('static const char *session_operation_name', start)]
        for name in ['handle_retain', 'handle_release', 'handle_snapshot', 'session_future_destroy',
                     'session_future_start', 'session_future_poll', 'session_future_cancel', 'session_future_timeout', 'session_future_expire']:
            code += extract(source, name)
        code += SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
        with tempfile.TemporaryDirectory() as tmp:
            source_path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            source_path.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source_path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_public_session/main.inc')
