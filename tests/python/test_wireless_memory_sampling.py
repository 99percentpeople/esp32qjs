"""Execute the device regression itself against the native API's lazy-tree shape.

Node only supplies the deterministic boundary fixture here; MQuickJS check-js
remains the syntax authority for the unchanged ES5 device source.
"""
import json
import pathlib
import shutil
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tests/js/flash_data/modules/wireless_core/lifecycle-existing-api.js'


class WirelessMemorySampling(unittest.TestCase):
    def run_fixture(self, leak, cleanup_pending=False):
        node = shutil.which('node')
        self.assertIsNotNone(node, 'Node is required for the lazy getter boundary fixture')
        fixture = r'''
var cycles = 0;
function gc() {}
function sleep() {}
var region = function () { return {freeBytes: 100000-cycles*16, largestFreeBlockBytes: 64000}; };
var memory = {};
['internal','psram','dma','default'].forEach(function (name) {
  Object.defineProperty(memory,name,{enumerable:true,get:region});
});
Object.defineProperty(memory,'manager',{enumerable:true,get:function () { return {
  managedPsramBytes: LEAK ? cycles*16 : 0, managedInternalBytes:0
}; }});
var sys = {status:{memory:memory}, tasks:function () {return {truncated:false,tasks:CLEANUP_PENDING ? [{state:'deleted'}] : []};}};
var ble = {open:function () { cycles++; return {
  scan:function () { return {stats:function () {return {dropped:0};}, close:function () {return true;}}; },
  close:function () {return true;}
};}};
var espNow = {open:function () {return {status:function () {return {open:true};},close:function () {}};}};
var wifi = {
  status:function () {return {radio:{clients:{total:0,espNow:0,wifiCsi:0}}};},
  csi:{capabilities:function () {return {configSchema:'wifi-csi-he/1'};},
    open:function () {return {close:function () {},stats:function () {return {leasedFrames:0};}};}}
};
function test(name, run) {
  try { console.log(JSON.stringify({value:run()})); }
  catch (error) { console.log(JSON.stringify({error:error.message})); }
}
'''.replace('LEAK', 'true' if leak else 'false').replace(
            'CLEANUP_PENDING', 'true' if cleanup_pending else 'false')
        harness = (ROOT / 'tests/js/flash_data/_test/harness.js').read_text()
        start = harness.index('  helper.memorySnapshot = function () {')
        end = harness.index('\n  };', start) + len('\n  };')
        sampler = harness[start:end].replace('helper.memorySnapshot', 'test.memorySnapshot')
        result = subprocess.run([node, '-e', fixture+sampler+SOURCE.read_text()], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_samples_are_captured_at_the_two_measurement_points(self):
        value = self.run_fixture(False)['value']
        self.assertEqual(value['before']['internal']['freeBytes'], 99984)
        self.assertEqual(value['after']['internal']['freeBytes'], 99904)

    def test_managed_owner_leak_is_detected_despite_lazy_getters(self):
        self.assertEqual(self.run_fixture(True).get('error'), 'managed PSRAM payload imbalance')

    def test_pending_native_task_cleanup_is_not_reported_as_a_settled_sample(self):
        self.assertEqual(self.run_fixture(False, cleanup_pending=True).get('error'),
                         'native task cleanup did not settle within 1000 ms')
