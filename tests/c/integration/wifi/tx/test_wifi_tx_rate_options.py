"""Deferred MQuickJS capture/conversion with production rate helpers; AST only now."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest

from tests.c.integration.wifi.tx.test_wifi_tx_rate import COMPONENT, rate_code
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WiFiTxRateOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code = rate_code('esp32c5/representative')
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract(source, 'esp32_mquickjs_wifi_tx_rate_capture') + extract(source, 'tx_rate_status_to_js')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_types_getter_once_original_exception_and_gc_oom(self):
        cases = [
            ('({phy:"11g",rate:"6m"})', True),
            ('({phy:"he20",rate:"mcs0-short",ersu:true,dcm:false})', True),
            ('({phy:"11g",rate:"6m",dcm:false,ersu:false})', True),
            ('({phy:"11g",rate:"6m",dcm:0})', False),
            ('({phy:"11g",rate:"6m",ersu:null})', False),
            ('({phy:"11g",rate:"6m",dcm:true})', False),
            ('({phy:"ht20",rate:"mcs9-long"})', False),
            ('({phy:"ht20",rate:"6m"})', False),
            ('({phy:"11g",rate:"6m\\u0000"})', False),
            ('({phy:"11g\\u0000",rate:"6m"})', False),
            ('({phy:"11g",rate:8})', False),
            ('({phy:"11g"})', False), ('[]', False), ('null', False),
            ('({phy:"11g",rate:"6m",extra:1})', False),
            ('({get phy(){throw new Error("sentinel");},rate:"6m"})', False),
            ('({phy:"11g",get rate(){throw new Error("sentinel");}})', False),
            ('({phy:"11g",rate:"6m",get dcm(){throw new Error("sentinel");}})', False),
            ('(function(){var n=0,m=0;return {get phy(){if(++n!==1)throw new Error("twice");return "11g";},get rate(){if(++m!==1)throw new Error("twice");return "6m";}};})()', True),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), expression, str(int(expected)), 'sentinel' if 'sentinel' in expression else ''])

    def test_known_unknown_and_stale_record_converter_gc_oom(self):
        for case in ('known', 'unknown', 'stale', 'temporary'):
            run([str(self.binary), case, '1', ''])


MAIN = fixture_text('wifi/tx/test_wifi_tx_rate_options/main.inc')
