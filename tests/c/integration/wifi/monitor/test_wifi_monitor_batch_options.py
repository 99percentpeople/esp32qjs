"""Deferred production Monitor Batch option parser in the actual moving VM."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import build, run
from tests.c.integration.wifi.monitor.test_wifi_monitor_options import production_options_code


class WiFiMonitorBatchOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = build(cls.temp.name, production_options_code(0), MAIN)

    def check(self, expression, pool=16, valid=True):
        run([str(self.binary), expression, str(pool), str(int(valid))])

    def test_defaults_and_aggregation_limits(self):
        for pool in [1, 16, 32, 128]:
            self.check('undefined', pool)
            self.check('({})', pool)
            self.check('({maximumFrames:' + str(pool) + ',minimumFrames:' + str(pool) +
                       ',timeoutMs:0,maximumLatencyMs:2147483647})', pool)
        self.check('({maximumFrames:5,minimumFrames:3,timeoutMs:2147483647,maximumLatencyMs:0})')
        self.check('({timeoutMs:undefined,maximumLatencyMs:undefined})')

    def test_strict_fields_numbers_null_and_pool_bounds(self):
        for expression in ['null', 'true', '[]', '1', '"batch"', '({extra:1})',
                           '({"timeoutMs\\x00":1})', '({minimumFrames:3,maximumFrames:2})']:
            self.check(expression, valid=False)
        for name in ['maximumFrames', 'minimumFrames']:
            for value in ['0','17','-1','1.5','NaN','Infinity','null','true','"2"']:
                self.check('({' + name + ':' + value + '})', valid=False)
        for name in ['timeoutMs', 'maximumLatencyMs']:
            for value in ['-1','2147483648','0.5','NaN','Infinity','null','false','"0"']:
                self.check('({' + name + ':' + value + '})', valid=False)
        self.check('undefined', pool=0, valid=False)
        self.check('undefined', pool=129, valid=False)


MAIN = fixture_text('wifi/monitor/test_wifi_monitor_batch_options/main.inc')
