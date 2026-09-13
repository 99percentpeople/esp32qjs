"""Deferred production ROC producer, numeric dispatch and native retirement.

The native driver, task/critical-section boundary and eloop admission are
controlled here. This implementation wave only AST-parses the fixture.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
PARTS = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp'


def strip_includes(source):
    return re.sub(r'^#(?:include|pragma once).*\n', '', source, flags=re.M)


class DppRocControl(unittest.TestCase):
    def test_native_completion_reuse_cancellation_and_failed_admission(self):
        header = strip_includes((ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_dpp_result.h').read_text())
        production = strip_includes((PARTS / 'esp32_mquickjs_dpp_roc.inc').read_text())
        compile_run(self, TYPES + header + BOUNDARIES + production + MAIN)


TYPES = fixture_text('wifi/provisioning/dpp/test_dpp_roc_control/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_dpp_roc_control/boundaries.inc')

MAIN = fixture_text('wifi/provisioning/dpp/test_dpp_roc_control/main.inc')
