"""Fault injection and real moving GC for production BLE value converters."""
from tests.support.fixtures import fixture_text
import pathlib
import subprocess
import sys
import tempfile
import unittest
from tests.c.integration.ble.test_ble_status_gc_regression import ROOT, SDK, build_checker, compiler_command, VENDOR_DIR, function
EXTRA=fixture_text('ble/test_ble_value_gc_regression/extra.inc')
MAIN=fixture_text('ble/test_ble_value_gc_regression/main.inc')
class BleValueGcRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        directory=pathlib.Path(cls.temp.name);headers=ROOT/'build/js-syntax';build_checker(headers)
        prefix=(ROOT/'scripts/codegen/mquickjs_syntax_check.c').read_text().split('#define CHECKER_HEAP_SIZE')[0]
        core=(ROOT/'components/esp32_mquickjs/src/core/esp32_mquickjs.c').read_text()
        start=core.index('bool esp32_mquickjs_set_property_ref(')
        helper=core[start:core.index('\n}\n',start)+3]
        ble=(ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c').read_text()
        bodies='\n'.join(function(ble,n) for n in ['ble_address_to_js','ble_security_to_js',
            'ble_connection_snapshot','ble_connection_event_to_js','ble_properties_to_js','ble_connection_status_to_js',
            'ble_service_index_for_characteristic','ble_characteristic_index_for_descriptor','ble_discovery_service_record',
            'ble_discovery_characteristic_record','ble_discovery_descriptor_record','ble_discover_finish'])
        source=directory/'values.c'
        sdk=SDK.replace('uint8_t address[6];','int lock;unsigned generation;uint8_t address[6];').replace(
            'struct { bool open; } connections[2];','ble_connection_slot_t connections[2];')
        split=EXTRA.index('static JSValue new_int64')
        source.write_text(prefix+'\n#include <stdbool.h>\n#include <stdatomic.h>\n'+EXTRA[:split]+sdk+EXTRA[split:]+helper+bodies+MAIN)
        cls.binary=directory/'test'
        result=subprocess.run([*compiler_command(),'-O1','-D_GNU_SOURCE',f'-I{VENDOR_DIR}',f'-I{headers}',
            f'-I{ROOT / "components/esp32_mquickjs/include"}',str(source),
            *(str(VENDOR_DIR/n) for n in ['mquickjs.c','dtoa.c','libm.c','cutils.c']),'-lm','-o',str(cls.binary)],capture_output=True,text=True)
        if result.returncode:raise AssertionError(result.stderr)

    def run_case(self,mode,gc):
        result=subprocess.run([str(self.binary),str(mode),str(int(gc))],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_gatt_properties_nth_failure(self):self.run_case(0,False)
    def test_gatt_properties_moving_gc(self):self.run_case(0,True)
    def test_address_security_connection_status_and_control_events(self):
        for mode in range(1,10):
            for gc in [False,True]:
                with self.subTest(mode=mode,gc=gc):self.run_case(mode,gc)

    def test_gatt_snapshot_allocation_failure_and_moving_gc(self):
        for gc in [False,True]:
            with self.subTest(gc=gc):self.run_case(10,gc)
