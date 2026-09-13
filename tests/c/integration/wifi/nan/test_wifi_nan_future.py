"""Deferred production NAN Future callbacks with the complete native Session.

Radio, worker dispatch and JS error construction are injected boundaries.
This file is not a second lifecycle implementation and is not RF evidence.
"""
from tests.support.fixtures import fixture_text
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from tests.c.integration.wifi.nan.test_wifi_nan_session import BASE, PREFIX, BOUNDARY, without_includes
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.nan.test_wifi_nan_path import HELPER as PATH_HELPER


class NanFuture(unittest.TestCase):
    def test_pairing_receive_wait_timeout_and_cancel_return_request_to_service(self):
        from tests.c.integration.wifi.nan.test_wifi_nan_bootstrap import PUBLISH_HELPER, BOOTSTRAP_HELPER
        self.compile_case(PUBLISH_HELPER + BOOTSTRAP_HELPER + fixture_text('wifi/nan/test_wifi_nan_future/test_pairing_receive_wait_timeout_and_cancel_return_request_to_service.inc'), pairing=True)

    def test_connection_wait_timeout_cancel_and_receive_reservation_keep_native_owner(self):
        self.compile_case(PATH_HELPER + fixture_text('wifi/nan/test_wifi_nan_future/test_connection_wait_timeout_cancel_and_receive_reservation_keep_native_owner.inc'))

    def test_pairing_wait_timeout_does_not_revoke_confirmation_or_close_owner(self):
        from tests.c.integration.wifi.nan.test_wifi_nan_pairing_session import HELPER
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_future/test_pairing_wait_timeout_does_not_revoke_confirmation_or_close_owner.inc'), pairing=True)

    def test_wait_cancel_timeout_and_close_retains_native_cleanup(self):
        self.compile_case(MAIN)

    def test_service_wait_cancel_and_timeout_do_not_release_native_ownership(self):
        self.compile_case(SERVICE_MAIN)

    def compile_case(self, main, *, pairing=False):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler unavailable')
        source = PREFIX
        if pairing:
            source = source.replace("#define CONFIG_ESP_WIFI_NAN_SECURITY 0", "#define CONFIG_ESP_WIFI_NAN_SECURITY 1")
            source += "\n#define CONFIG_ESP_WIFI_NAN_PAIRING 1\n"
        for name in ('esp32_mquickjs_wifi_nan_pasn_sdk.h', 'esp32_mquickjs_wifi_nan_sdk.h', 'esp32_mquickjs_wifi_nan_ndp.h', 'esp32_mquickjs_wifi_nan_tx.h', 'esp32_mquickjs_wifi_nan_radio.h',
                     'esp32_mquickjs_wifi_nan_session.h', 'esp32_mquickjs_wifi_nan_discovery.h', 'esp32_mquickjs_wifi_nan_message.h', 'esp32_mquickjs_wifi_nan_path.h', 'esp32_mquickjs_wifi_nan_pairing.h'):
            source += without_includes(BASE / 'internal' / name)
        source += BOUNDARY
        if pairing:
            from tests.c.integration.wifi.nan.test_wifi_nan_pairing_session import PAIRING_BOUNDARY
            source += PAIRING_BOUNDARY
        source += without_includes(BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_session.c')
        future = (BASE / 'internal/esp32_mquickjs_future.h').read_text()
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            source += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan.c').read_text()
        source += re.search(r'typedef enum \{[^}]*\} nan_operation_t;', public).group(0)
        source += re.search(r'typedef struct \{[^}]*\} nan_path_response_t;', public).group(0)
        start = public.index('struct esp32_mquickjs_future_driver_state {')
        source += public[start:public.index('\n};', start) + 3]
        source += fixture_text('wifi/nan/test_wifi_nan_future/compile_case.inc')
        for name in ('nan_operation_name', 'nan_destroy', 'nan_start', 'nan_poll',
                     'nan_cancel_wait', 'nan_timeout', 'nan_on_timeout'):
            source += extract(public, name)
        with tempfile.TemporaryDirectory() as folder:
            path, executable = Path(folder) / 'case.c', Path(folder) / 'case'
            path.write_text(source + main)
            result = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                str(path), '-o', str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = fixture_text('wifi/nan/test_wifi_nan_future/main.inc')

SERVICE_MAIN = fixture_text('wifi/nan/test_wifi_nan_future/service_main.inc')
