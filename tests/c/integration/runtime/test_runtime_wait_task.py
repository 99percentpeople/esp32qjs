"""Run production wait/watchdog hooks with runtime and worker task identities."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.support.wireless_vm_fixture import extract
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
CORE = ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs.c'
HOST = ROOT / 'components/esp32qjs_runtime/src/esp32qjs_runtime.c'

BOUNDARIES = fixture_text('runtime/test_runtime_wait_task/boundaries.inc')

MAIN = fixture_text('runtime/test_runtime_wait_task/main.inc')

class RuntimeWaitTaskTests(unittest.TestCase):
    def test_worker_cannot_change_js_wait_or_deadline(self):
        self.run_case(True)

    def test_only_runtime_task_feeds_its_watchdogs(self):
        self.run_case(False)

    def run_case(self, wait):
        core, host = CORE.read_text(), HOST.read_text()
        code = BOUNDARIES
        if 'static bool esp32_mquickjs_runtime_task_is_current(' in core:
            code += extract(core, 'esp32_mquickjs_runtime_task_is_current')
        for name in ('runtime_cooperate','runtime_feed_js_watchdog'):
            code += extract(host,name)
        for name in ('esp32_mquickjs_cooperate','esp32_mquickjs_native_wait_begin','esp32_mquickjs_native_wait_end'):
            code += extract(core,name)
        compile_run(self, code+'\n#define TEST_WAIT '+str(int(wait))+'\n'+MAIN)
