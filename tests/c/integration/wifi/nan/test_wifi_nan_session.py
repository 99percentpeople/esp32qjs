"""Controlled scheduling of the production NAN Session. Execution is deferred."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = TEST_ROOT
BASE = ROOT / 'components/esp32_mquickjs'


def without_includes(path):
    source = path.read_text()
    if path.name == 'esp32_mquickjs_wifi_nan_radio.h':
        from tests.c.integration.wifi.nan.test_wifi_nan_query import query_types
        source = query_types() + source
    if path.name == 'esp32_mquickjs_wifi_nan_discovery.h':
        source = (BASE / 'internal/esp32_mquickjs_wifi_nan_service_config.h').read_text() + '\n' + source
    if path.name == 'esp32_mquickjs_wifi_nan_session.c':
        for name in ('esp32_mquickjs_wifi_nan_discovery.inc', 'esp32_mquickjs_wifi_nan_message.inc', 'esp32_mquickjs_wifi_nan_path.inc', 'esp32_mquickjs_wifi_nan_pairing.inc', 'esp32_mquickjs_wifi_nan_credentials.inc', 'esp32_mquickjs_wifi_nan_bootstrap.inc'):
            source = source.replace('#include "' + name + '"', (path.parent / name).read_text())
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


class NanSessionLifecycle(unittest.TestCase):
    def test_query_requires_active_ready_session_and_serializes_outside_critical(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_session/test_query_requires_active_ready_session_and_serializes_outside_critical.inc'))

    def test_native_timer_or_pool_fault_starts_close_without_observation_event(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_session/test_native_timer_or_pool_fault_starts_close_without_observation_event.inc'))

    def test_production_session_oom_queue_timeout_close_and_runtime_teardown(self):
        self.compile_case(MAIN)

    def test_production_discovery_early_event_cancel_retirement_and_gc_owners(self):
        self.compile_case(DISCOVERY_MAIN)

    def test_production_message_timeout_late_completion_and_parent_close(self):
        self.compile_case(MESSAGE_MAIN)

    def test_usd_only_admission_and_failed_cleanup_retain_native_owner(self):
        self.compile_case(fixture_text('wifi/nan/test_wifi_nan_session/test_usd_only_admission_and_failed_cleanup_retain_native_owner.inc'), usd_only=True)

    def compile_case(self, main, *, security=False, usd_only=False, pairing=False):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler unavailable')
        source = PREFIX.replace("#define CONFIG_ESP_WIFI_NAN_SECURITY 0", "#define CONFIG_ESP_WIFI_NAN_SECURITY 1") if security else PREFIX
        if pairing:
            source += "\n#define CONFIG_ESP_WIFI_NAN_PAIRING 1\n"
        if usd_only:
            source = source.replace('#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1',
                '#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 0\n#define CONFIG_ESP_WIFI_NAN_USD_ENABLE 1')
        for name in ('esp32_mquickjs_wifi_nan_pasn_sdk.h', 'esp32_mquickjs_wifi_nan_sdk.h', 'esp32_mquickjs_wifi_nan_ndp.h', 'esp32_mquickjs_wifi_nan_tx.h', 'esp32_mquickjs_wifi_nan_radio.h',
                     'esp32_mquickjs_wifi_nan_session.h', 'esp32_mquickjs_wifi_nan_discovery.h', 'esp32_mquickjs_wifi_nan_message.h', 'esp32_mquickjs_wifi_nan_path.h', 'esp32_mquickjs_wifi_nan_pairing.h'):
            source += without_includes(BASE / 'internal' / name)
        source += BOUNDARY
        if pairing:
            from tests.c.integration.wifi.nan.test_wifi_nan_pairing_session import PAIRING_BOUNDARY
            source += PAIRING_BOUNDARY
        source += without_includes(BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_session.c')
        source += main
        with tempfile.TemporaryDirectory() as folder:
            path, executable = Path(folder) / 'case.c', Path(folder) / 'case'
            path.write_text(source)
            result = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                str(path), '-o', str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)


PREFIX = fixture_text('wifi/nan/test_wifi_nan_session/prefix.inc')

BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_session/boundary.inc')

SERVICE_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_session/service_boundary.inc')
MESSAGE_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_session/message_boundary.inc')
PATH_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_session/path_boundary.inc')
BOUNDARY += SERVICE_BOUNDARY + MESSAGE_BOUNDARY + '\n#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE\n' + PATH_BOUNDARY + '\n#endif\n'

DISCOVERY_MAIN = fixture_text('wifi/nan/test_wifi_nan_session/discovery_main.inc')

MESSAGE_MAIN = fixture_text('wifi/nan/test_wifi_nan_session/message_main.inc')

MAIN = fixture_text('wifi/nan/test_wifi_nan_session/main.inc')
