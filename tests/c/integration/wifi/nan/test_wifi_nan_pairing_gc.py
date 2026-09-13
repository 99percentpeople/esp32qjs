"""Deferred real MQuickJS pairing PIN and status conversion under moving GC/OOM."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.nan.test_wifi_nan_session import BASE, PREFIX, without_includes
from tests.support.wireless_vm_fixture import build, extract, run


class NanPairingGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        code = PREFIX + '\n#define CONFIG_ESP_WIFI_NAN_PAIRING 1\n#include "mquickjs_priv.h"\n'
        for name in ('esp32_mquickjs_wifi_nan_pasn_sdk.h', 'esp32_mquickjs_wifi_nan_sdk.h',
                     'esp32_mquickjs_wifi_nan_ndp.h', 'esp32_mquickjs_wifi_nan_radio.h',
                     'esp32_mquickjs_wifi_nan_session.h', 'esp32_mquickjs_wifi_nan_discovery.h',
                     'esp32_mquickjs_wifi_nan_pairing.h'):
            code += without_includes(BASE / 'internal' / name)
        code += 'static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}\n'
        public = BASE / 'src/modules/wifi_nan'
        code += re.search(r'^#define SET\(.*$', (public / 'esp32_mquickjs_wifi_nan.c').read_text(), re.M).group(0) + '\n'
        code += extract((public / 'esp32_mquickjs_wifi_nan_service_public.inc').read_text(), 'nan_event_bytes')
        for name in ('nan_pairing_pin', 'nan_pairing_status_to_js', 'nan_credentials_to_js'):
            code += extract((public / 'esp32_mquickjs_wifi_nan_pairing_public.inc').read_text(), name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_pin_exact_digits_and_snapshot_each_allocation_failure(self):
        for value, expected in [('"001234"', 1), ('"000000"', 1), ('123456', 0),
                                ('"12345"', 0), ('"1234567"', 0), ('"12a456"', 0),
                                ('"12345\\u0000"', 0), ('undefined', 0)]:
            with self.subTest(value=value):
                run([str(self.binary), value, str(expected)])


MAIN = fixture_text('wifi/nan/test_wifi_nan_pairing_gc/main.inc')
