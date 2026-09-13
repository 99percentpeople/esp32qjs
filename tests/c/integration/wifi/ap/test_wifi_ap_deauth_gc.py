"""Deferred actual VM deauth parser/error conversion, moving GC and allocation failures."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run


class WiFiAPDeauthGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
        wireless = (CORE / 'esp32_mquickjs_wireless_core.c').read_text()
        code = BOUNDARIES
        for name in ('wireless_hex_nibble', 'esp32_mquickjs_wireless_parse_address'):
            code += extract(wireless, name)
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += extract(source, 'js_wifi_deauth_client')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_inputs_and_error_allocation_do_not_repeat_native_mutation(self):
        for expression, args, valid in [
            ('"02:ab:cd:00:ef:01"', 1, True), ('"02:AB:CD:00:EF:01"', 1, True),
            ('null', 1, False), ('1', 1, False), ('"00:00:00:00:00:00"', 1, False),
            ('"ff:ff:ff:ff:ff:ff"', 1, False), ('"01:00:00:00:00:01"', 1, False),
            ('"02:ab:cd:00:ef:01\\u0000"', 1, False), ('"02:ab:cd:00:ef:01x"', 1, False),
            ('"02:ab:cd:00:ef:gg"', 1, False), ('"02:ab:cd:00:ef:01"', 0, False),
            ('"02:ab:cd:00:ef:01"', 2, False),
        ]:
            for mode in range(6):
                with self.subTest(expression=expression, args=args, mode=mode):
                    run([str(self.binary), expression, str(args), str(int(valid)), str(mode)])


BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_deauth_gc/boundaries.inc')

MAIN = fixture_text('wifi/ap/test_wifi_ap_deauth_gc/main.inc')
