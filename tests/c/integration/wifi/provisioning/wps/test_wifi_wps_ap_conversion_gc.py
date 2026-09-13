"""Deferred AP production converters + Session copy/commit in the real VM.

Inject GC and every JS allocation failure. Runtime/RF scheduling remains a
separate test obligation. Do not execute until concentrated Wi-Fi validation.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.provisioning.wps.test_wifi_wps_ap_session import COMPONENT, TYPES, headers
from tests.c.integration.wifi.provisioning.wps.test_wifi_wps_capture_gc import BOUNDARIES
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WpsAPConversionGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap.c').read_text()
        native = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_session.c').read_text()
        worker = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_worker.c').read_text()
        code = TYPES + headers() + BOUNDARIES
        a = native.index('struct esp32_mquickjs_wifi_wps_ap_session {')
        b = native.index('static void wps_session_close_locked', a)
        code += native[a:b]
        code += extract(worker, 'esp32_mquickjs_wifi_wps_config_valid')
        for name in ('wps_session_close_locked', 'esp32_mquickjs_wifi_wps_ap_open_runtime',
                     'esp32_mquickjs_wifi_wps_ap_session_create', 'esp32_mquickjs_wifi_wps_ap_session_retain',
                     'esp32_mquickjs_wifi_wps_ap_session_release', 'esp32_mquickjs_wifi_wps_ap_session_close',
                     'esp32_mquickjs_wifi_wps_ap_session_status', 'wps_session_copy',
                     'esp32_mquickjs_wifi_wps_ap_session_pin', 'esp32_mquickjs_wifi_wps_ap_session_registered'):
            code += extract(native, name)
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        code += 'typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define WPS_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        for name in ('wps_identity', 'wps_status_to_js', 'wps_watch_to_js',
                     'wps_error', 'wps_pin_to_js', 'wps_registered_to_js'):
            code += extract(public, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_gc_and_every_js_allocation_failure_preserve_unconsumed_results(self):
        for mode in ('pin', 'registered', 'status', 'observation', 'error', 'closed', 'consumed'):
            with self.subTest(mode=mode): run([str(self.binary), mode])


MAIN = fixture_text('wifi/provisioning/wps/test_wifi_wps_ap_conversion_gc/main.inc')
