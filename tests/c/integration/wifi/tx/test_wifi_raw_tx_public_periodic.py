"""Deferred production Future controls over the native periodic/Session stack.

JS conversion and the generic Future scheduler are outside this C fixture.
"""
from tests.support.fixtures import fixture_text
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.tx.test_wifi_raw_tx_periodic_job import production_periodic_job_code, RAW
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import extract, INTERNAL


class WiFiRawTxPublicPeriodic(unittest.TestCase):
    def test_cancel_timeout_native_retention_and_retired_handle_detach(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (RAW / 'esp32_mquickjs_wifi_raw_tx_public_periodic.c').read_text()
        future = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
        code = production_periodic_job_code()
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t;\n'
        code += structure(future, 'esp32_mquickjs_future_token_t')
        for name in ['esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t']:
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        # Independent production translation units use private status_t aliases.
        code += '#define status_t public_periodic_status_t\n#define job_api(name) esp32_mquickjs_wifi_raw_tx_periodic_job_##name\n'
        start = source.index('typedef esp32_mquickjs_wifi_raw_tx_periodic_job_t job_t;')
        code += source[start:source.index('static periodic_handle_t *periodic_handle', start)]
        for name in ['handle_retain', 'handle_release', 'handle_snapshot', 'periodic_destroy',
                     'periodic_request_close', 'periodic_start', 'periodic_poll', 'periodic_cancel',
                     'periodic_timeout', 'periodic_expire']:
            code += extract(source, name)
        code += '#undef status_t\n'
        with tempfile.TemporaryDirectory() as directory:
            source_path, binary = Path(directory) / 'fixture.c', Path(directory) / 'fixture'
            source_path.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source_path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_public_periodic/main.inc')
