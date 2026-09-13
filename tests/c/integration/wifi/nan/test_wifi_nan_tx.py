"""Deferred production NAN SD/NDP tracker with injected native boundaries.

Host pointers model ledger ownership only. Target ABI and IRAM closure require
the configured C5 compiler/link checks and hardware acceptance separately.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = TEST_ROOT
BASE = ROOT / 'components/esp32_mquickjs'


class NanTx(unittest.TestCase):
    def test_actual_tracker_oom_quarantine_recycle_return_reuse_and_exhaustion(self):
        self.compile_case(MAIN)

    def test_message_scope_and_full_callback_retirement_survive_buffer_address_reuse(self):
        self.compile_case(MESSAGE_MAIN)

    def test_datapath_frames_share_capacity_and_wait_for_entire_native_callback(self):
        self.compile_case(DATAPATH_MAIN)

    def test_datapath_identity_early_binding_mutation_and_native_address_quarantine(self):
        self.compile_case(NDP_IDENTITY_MAIN)

    def test_pairing_auth_and_followup_share_pool_and_retire_after_producer_and_recycler(self):
        self.compile_case(PAIRING_MAIN, PAIRING_BOUNDARY, pairing=True)

    def compile_case(self, main, native='', pairing=False):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('Host C compiler unavailable')
        header = (BASE / 'internal/esp32_mquickjs_wifi_nan_pasn_sdk.h').read_text() + (BASE / 'internal/esp32_mquickjs_wifi_nan_tx.h').read_text()
        sdk = (BASE / 'internal/esp32_mquickjs_wifi_nan_sdk.h').read_text().split('#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE')[0]
        ndp = (BASE / 'internal/esp32_mquickjs_wifi_nan_ndp.h').read_text()
        timer = (BASE / 'internal/esp32_mquickjs_wifi_nan_timer.h').read_text()
        source = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_tx.c').read_text()
        source = source.replace('#include "esp32_mquickjs_wifi_nan_ndp.inc"',
            (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_ndp.inc').read_text())
        source = source.replace('#include "esp32_mquickjs_wifi_nan_timer.inc"',
            (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_timer.inc').read_text())
        source = source.replace('#include "esp32_mquickjs_wifi_nan_data.inc"',
            (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_data.inc').read_text())
        source = source.replace('#include "esp32_mquickjs_wifi_nan_pairing_tx.inc"',
            (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_tx.inc').read_text())
        code = ('#define CONFIG_ESP_WIFI_NAN_PAIRING 1\n' if pairing else '') + PREFIX + TIMER_BOUNDARY + sdk + ndp + timer + header + source
        code = re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', code, flags=re.M)
        # The Host fixture supplies native pointer layout; the original target
        # width assertion remains in production and in the C5 syntax check.
        code = code.replace('_Static_assert(sizeof(uintptr_t) == 4, "reviewed C5 NAN argument and EB layout");', '')
        code = re.sub(r'_Static_assert\(sizeof\(ETSTimer\).*?"reviewed C5 NAN timer layout"\);', '', code, flags=re.S)
        with tempfile.TemporaryDirectory() as folder:
            src, binary = Path(folder) / 'case.c', Path(folder) / 'case'
            src.write_text(INCLUDES + code + SEND_BOUNDARY + DATA_BOUNDARY + native + main)
            p = subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 0, p.stderr)
            p = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(p.returncode, 0, p.stderr)


INCLUDES = '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdlib.h>\n#include <string.h>\n'
PAIRING_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_tx/pairing_boundary.inc')
PAIRING_MAIN = fixture_text('wifi/nan/test_wifi_nan_tx/pairing_main.inc')
PREFIX = fixture_text('wifi/nan/test_wifi_nan_tx/prefix.inc')
SEND_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_tx/send_boundary.inc')
TIMER_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_tx/timer_boundary.inc')
DATA_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_tx/data_boundary.inc')
NDP_IDENTITY_MAIN = fixture_text('wifi/nan/test_wifi_nan_tx/ndp_identity_main.inc')
DATAPATH_MAIN = fixture_text('wifi/nan/test_wifi_nan_tx/datapath_main.inc')
MESSAGE_MAIN = fixture_text('wifi/nan/test_wifi_nan_tx/message_main.inc')
MAIN = fixture_text('wifi/nan/test_wifi_nan_tx/main.inc')
