"""Deferred execution of the patched production SDK one-shot wake function."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run
from tests.support.c_source import extract as function

ROOT = TEST_ROOT


class ElooopWake(unittest.TestCase):
    def test_failed_wake_rearms_retained_work_and_respects_teardown(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        sys.path.insert(0, str(ROOT / 'scripts'))
        from patch_idf_eloop import patch_source
        source = patch_source((Path(sdk) / 'components/wpa_supplicant/port/eloop.c').read_bytes()).decode()
        compile_run(self, BOUNDARIES + function(source, 'eloop_run_timer') + MAIN)


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_eloop_wake/boundaries.inc')

MAIN = fixture_text('wifi/lifecycle/test_wifi_eloop_wake/main.inc')
