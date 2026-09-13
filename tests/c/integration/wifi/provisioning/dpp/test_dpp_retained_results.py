"""Deferred production result ownership, SDK cleanup suffix and event fence.

Native driver/event-loop calls are controllable boundaries. No RF or RTOS
success is implied by this fixture, and execution remains deferred this wave.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import re
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
PARTS = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp'


def without_includes(text):
    return re.sub(r'^#(?:include|pragma once).*\n', '', text, flags=re.M)


class DppRetainedResults(unittest.TestCase):
    def test_copy_commit_terminal_identity_partial_cleanup_and_queue_fence(self):
        sdk_path = os.environ.get('IDF_PATH')
        if not sdk_path:
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        sys.path.insert(0, str(ROOT / 'scripts'))
        from patch_idf_dpp import function
        from tests.c.integration.wifi.provisioning.dpp.test_dpp_config_transaction import sdk_struct
        public = (Path(sdk_path) / 'components/esp_wifi/include/esp_wifi_types_generic.h').read_text()
        header = without_includes((ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_dpp_result.h').read_text())
        source = TYPES + sdk_struct(public, 'esp_dpp_config_data_t')
        source += sdk_struct(public, 'wifi_event_dpp_config_received_t') + header
        source += sdk_struct((ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_chm_timer.h').read_text(),
            'esp32qjs_wifi_chm_timer_status_t') + BOUNDARIES
        source += function((PARTS / 'esp32_mquickjs_dpp_config.inc').read_text(), 'esp32qjs_dpp_config_valid')
        source += without_includes((PARTS / 'esp32_mquickjs_dpp_result.inc').read_text())
        source += (PARTS / 'esp32_mquickjs_dpp_deinit.inc').read_text()
        compile_run(self, source + MAIN)


TYPES = fixture_text('wifi/provisioning/dpp/test_dpp_retained_results/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_dpp_retained_results/boundaries.inc')

MAIN = fixture_text('wifi/provisioning/dpp/test_dpp_retained_results/main.inc')
