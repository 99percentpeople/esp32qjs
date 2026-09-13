"""Run the production BLE status converter against the vendored moving-GC VM."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT=TEST_ROOT
sys.path.insert(0,str(ROOT/'scripts'))
from check_js_syntax import build_checker, compiler_command, VENDOR_DIR
from tests.support.c_source import extract as function

SDK = fixture_text('ble/test_ble_status_gc_regression/sdk.inc')
MAIN = fixture_text('ble/test_ble_status_gc_regression/main.inc')

class BleStatusGcRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        directory=pathlib.Path(cls.temporary.name)
        headers=ROOT/'build/js-syntax'
        build_checker(headers)
        prefix=(ROOT/'scripts/mquickjs_syntax_check.c').read_text().split('#define CHECKER_HEAP_SIZE')[0]
        core=(ROOT/'components/esp32_mquickjs/src/core/esp32_mquickjs.c').read_text()
        helper=''
        for name in ('esp32_mquickjs_set_property','esp32_mquickjs_set_property_ref'):
            start=core.index('bool '+name+'(')
            helper+=core[start:core.index('\n}\n',start)+3]
        ble=(ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c').read_text()
        source=directory/'status.c'
        source.write_text(prefix+SDK+helper+function(ble,'ble_adapter_status_to_js')+MAIN)
        cls.binary=directory/'test'
        command=[*compiler_command(),'-O1','-D_GNU_SOURCE',f'-I{VENDOR_DIR}',f'-I{headers}',
                 f'-I{ROOT / "components/esp32_mquickjs/include"}',str(source),
                 *(str(VENDOR_DIR/name) for name in ('mquickjs.c','dtoa.c','libm.c','cutils.c')),
                 '-lm','-o',str(cls.binary)]
        result=subprocess.run(command,capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stderr)

    def test_nth_allocation_failure_returns_exception(self):
        self.run_mode(0)

    def test_property_helpers_preserve_exception_without_storing_sentinel(self):
        self.run_mode(2)

    def test_nth_allocation_failure_and_forced_moving_gc(self):
        self.run_mode(1)

    def run_mode(self,mode):
        result=subprocess.run([str(self.binary),str(mode)],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('allocation-capable API boundaries',result.stdout)
