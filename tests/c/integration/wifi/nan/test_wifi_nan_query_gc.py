"""Deferred real-VM NAN query argument rooting, snapshots and Nth allocation.

Native cache availability is supplied at the Session boundary. The native SDK,
Session/Radio concurrency and shutdown gates have separate production fixtures.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.nan.test_wifi_nan_session import BASE, PREFIX, without_includes
from tests.c.integration.wifi.nan.test_wifi_nan_query import query_types
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class NanQueryGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = BASE / 'src/modules/wifi_nan'
        code = PREFIX + '\n#include <math.h>\n#include "mquickjs_priv.h"\n' + query_types()
        code += without_includes(CORE / 'esp32_mquickjs_options.c')
        code += re.search(r'^#define SET\(.*$', (public / 'esp32_mquickjs_wifi_nan.c').read_text(), re.M).group(0) + '\n'
        code += 'static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){memset(p,0,n);}\n'
        for name in ('nan_service_string', 'nan_event_bytes'):
            code += extract((public / 'esp32_mquickjs_wifi_nan_service_public.inc').read_text(), name)
        code += extract((public / 'esp32_mquickjs_wifi_nan_service_options_public.inc').read_text(), 'nan_option_bytes')
        code += BOUNDARY + (public / 'esp32_mquickjs_wifi_nan_query_public.inc').read_text()
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_production_query_conversion_and_argument_roots_every_allocation(self):
        cases = [
            ('service', '["alpha",null]', 1), ('service', '[7,null]', 1),
            ('peers', '["alpha",null]', 1), ('peer', '[undefined,[2,3,4,5,6,7]]', 1),
            ('peer', '["alpha",[2,3,4,5,6,7]]', 1),
            ('peer', '["alpha",[2,3,4,5,6,7].map(function(x){gc();return x;})]', 1),
            ('peer', '[7,[2,3,4,5,6,7]]', 1),
            ('service', '[0,null]', 0), ('service', '[256,null]', 0), ('service', '[NaN,null]', 0),
            ('service', '[1.5,null]', 0), ('service', '[true,null]', 0), ('service', '[null,null]', 0),
            ('service', '["",null]', 0), ('service', '["alpha\\u0000",null]', 0),
            ('service', '[Array(257).join("x"),null]', 0),
            ('peer', '[7,[2,3,4]]', 0), ('peer', '[7,[0,0,0,0,0,0]]', 0),
            ('peer', '[7,[3,3,4,5,6,7]]', 0), ('peer', '[7,[2,3,4,5,6,256]]', 0),
            ('peer', '[7,[2,3,4,5,6,7,8]]', 0),
        ]
        for mode, expression, expected in cases:
            with self.subTest(mode=mode, expression=expression):
                run([str(self.binary), mode, expression, str(expected), '0'])

    def test_missing_and_native_failure_release_query_reference(self):
        for error in ('107', '102', '105'):
            run([str(self.binary), 'peers', '[7,null]', '1' if error == '107' else '0', error])


BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_query_gc/boundary.inc')

MAIN = fixture_text('wifi/nan/test_wifi_nan_query_gc/main.inc')
