"""Deferred real VM periodic capture with the production ByteSource helper."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest

from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, unit
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import CORE, build, extract, run

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


class WiFiRawTxPeriodicOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        extra = unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_periodic.h')
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        extra += BOUNDARY
        extra += extract((RAW / 'esp32_mquickjs_wifi_raw_tx.c').read_text(), 'esp32_mquickjs_wifi_raw_tx_capture_bytes')
        source = (RAW / 'esp32_mquickjs_wifi_raw_tx_public_periodic.c').read_text()
        extra += extract(source, 'periodic_options')
        extra += 'typedef int esp_err_t;\n'
        extra += structure((INTERNAL / 'esp32_mquickjs_wifi_raw_tx_periodic_job.h').read_text(), 'esp32_mquickjs_wifi_raw_tx_periodic_job_status_t')
        extra += 'typedef esp32_mquickjs_wifi_raw_tx_periodic_job_status_t status_t;\n'
        extra += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        extra += extract(source, 'periodic_status_to_js')
        cls.binary = build(cls.temp.name, extra, MAIN)

    def test_required_interval_optional_bounds_bool_and_getter_ownership(self):
        cases = [
            ('({frame:view,intervalUs:1000})', True),
            ('({frame:view,intervalUs:4294967295,count:4294967295,startDelayUs:4294967295,busyPolicy:"stop",stopOnError:false,timeoutMs:60000})', True),
            ('({frame:view})', False), ('({frame:view,intervalUs:999})', False),
            ('({frame:view,intervalUs:1000.5})', False), ('({frame:view,intervalUs:4294967296})', False),
            ('({frame:view,intervalUs:1000,count:-1})', False),
            ('({frame:view,intervalUs:1000,count:4294967296})', False),
            ('({frame:view,intervalUs:1000,startDelayUs:-1})', False),
            ('({frame:view,intervalUs:1000,timeoutMs:0})', False),
            ('({frame:view,intervalUs:1000,timeoutMs:60001})', False),
            ('({frame:view,intervalUs:1000,busyPolicy:"skip\\u0000"})', False),
            ('({frame:view,intervalUs:1000,stopOnError:0})', False),
            ('({frame:view,intervalUs:1000,unknown:true})', False), ('null', False), ('[]', False),
            ('({intervalUs:1000,get frame(){throw new Error("sentinel");}})', False),
            ('({frame:view,get intervalUs(){throw new Error("sentinel");}})', False),
            ('({frame:view,get intervalUs(){view.close();return 1000;}})', False),
            ('(function(){var n=0,m=0;return {get frame(){if(++n!==1)throw new Error("twice");return view;},get intervalUs(){if(++m!==1)throw new Error("twice");return 1000;}};})()', True),
        ]
        for expression, valid in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), expression, str(int(valid)),
                     'sentinel' if 'sentinel' in expression else '', 'false' if 'stopOnError:false' in expression else 'true'])

    def test_status_converter_moving_gc_and_nth_allocation(self):
        run([str(self.binary), 'status', '1', '', 'true'])


BOUNDARY = fixture_text('wifi/tx/test_wifi_raw_tx_periodic_options/boundary.inc')
MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_periodic_options/main.inc')
