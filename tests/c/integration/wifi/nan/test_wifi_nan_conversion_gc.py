"""Deferred real-VM NAN options/status/errors: production code, moving GC, Nth OOM.

Native readiness is supplied at the status boundary; this does not simulate
NAN protocol completion or replace the production Session scheduling fixture.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.nan.test_wifi_nan_session import BASE, PREFIX, without_includes
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class NanConversionGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        code = PREFIX + '\n#include <math.h>\n#include "mquickjs_priv.h"\n'
        for name in ('esp32_mquickjs_wifi_nan_sdk.h', 'esp32_mquickjs_wifi_nan_ndp.h', 'esp32_mquickjs_wifi_nan_tx.h', 'esp32_mquickjs_wifi_nan_radio.h',
                     'esp32_mquickjs_wifi_nan_session.h', 'esp32_mquickjs_wifi_nan_discovery.h', 'esp32_mquickjs_wifi_nan_message.h', 'esp32_mquickjs_wifi_nan_path.h'):
            code += without_includes(BASE / 'internal' / name)
        code += fixture_text('wifi/nan/test_wifi_nan_conversion_gc/setupclass.inc')
        native = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_session.c').read_text()
        start = native.index('struct esp32_mquickjs_wifi_nan_session {')
        code += native[start:native.index('static void nan_message_worker', start)]
        for name in ('nan_session_close_locked', 'esp32_mquickjs_wifi_nan_open_runtime',
                     'esp32_mquickjs_wifi_nan_global_status', 'nan_session_create',
                     'esp32_mquickjs_wifi_nan_session_create',
                     'esp32_mquickjs_wifi_nan_session_release', 'esp32_mquickjs_wifi_nan_session_status'):
            code += extract(native, name)
        code += without_includes(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan.c').read_text()
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        for name in ('nan_open_options', 'nan_status_to_js', 'nan_error', 'nan_tx_status_to_js',
                     'js_wifi_nan_global_status', 'js_wifi_nan_capabilities'):
            code += extract(public, name)
        service_public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_service_public.inc').read_text()
        for name in ('nan_service_status_to_js', 'nan_event_bytes', 'nan_service_event_to_js'):
            code += extract(service_public, name)
        message_public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_message_public.inc').read_text()
        code += extract(message_public, 'nan_message_result_to_js')
        path_public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_path_public.inc').read_text()
        code += extract(path_public, 'nan_path_status_to_js')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_options_and_getter_gc_before_native_admission(self):
        cases = [('undefined', 1), ('({})', 1), ('null', 0), ('[]', 0),
                 ('({mode:"unsynchronized"})', 1), ('({mode:"unsynchronized",channel:6})', 0),
                 ('({mode:"unknown"})', 0), ('({mode:"unsynchronized",get timeoutMs(){gc();return 1000;}})', 1),
                 ('({channel:0})', 0), ('({channel:256})', 0), ('({channel:149})', 1),
                 ('({channel:"6"})', 0), ('({channel:NaN})', 0), ('({channel:Infinity})', 0),
                 ('({channel:6.5})', 0), ('({masterPreference:256})', 0), ('({scanTimeSeconds:-1})', 0),
                 ('({warmUpSeconds:65536})', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:120001})', 0),
                 ('({randomizeMac:1})', 0), ('({resetCurrentNvsCreds:true})', 0),
                 ('({channel:255,masterPreference:255,scanTimeSeconds:255,warmUpSeconds:65535,timeoutMs:120000,randomizeMac:false})', 1),
                 ('({get channel(){gc();return 6;},get timeoutMs(){gc();return 1000;}})', 1),
                 ('({get channel(){gc();throw new Error("sentinel");}})', 0)]
        for value, expected in cases:
            with self.subTest(value=value):
                run([str(self.binary), 'options', value, str(expected)])

    def test_status_capability_and_error_conversion_gc_and_nth_allocation(self):
        for mode in ('status', 'global', 'capabilities', 'error', 'service-status', 'service-event', 'message-result', 'path-status'):
            with self.subTest(mode=mode):
                run([str(self.binary), mode, 'undefined', '1'])


MAIN = fixture_text('wifi/nan/test_wifi_nan_conversion_gc/main.inc')
