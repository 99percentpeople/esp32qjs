"""Deferred real-VM WPS options and PIN/credentials conversion, Nth OOM and GC.

Links production Session copy/commit with injected result bytes. Does not model
RF negotiation or prove full Future/event reaper scheduling. AST only until the
concentrated Wi-Fi validation phase.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.provisioning.wps.test_wifi_wps_session import COMPONENT, TYPES, headers, unit
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WpsCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps.c').read_text()
        native = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_session.c').read_text()
        worker = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_worker.c').read_text()
        code = TYPES + headers() + BOUNDARIES
        a = native.index('struct esp32_mquickjs_wifi_wps_session {')
        b = native.index('static void wps_session_close_locked', a)
        code += native[a:b]
        code += extract(worker, 'esp32_mquickjs_wifi_wps_config_valid')
        for name in ('wps_session_close_locked', 'esp32_mquickjs_wifi_wps_open_runtime',
                     'esp32_mquickjs_wifi_wps_session_create', 'esp32_mquickjs_wifi_wps_session_retain',
                     'esp32_mquickjs_wifi_wps_session_release', 'esp32_mquickjs_wifi_wps_session_close',
                     'esp32_mquickjs_wifi_wps_session_status', 'wps_session_copy',
                     'esp32_mquickjs_wifi_wps_session_pin', 'esp32_mquickjs_wifi_wps_session_credentials'):
            code += extract(native, name)
        code += re.search(r'typedef struct \{[^}]*\} wps_options_t;', public).group(0)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        code += 'typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define WPS_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        for name in ('wps_string', 'wps_options_for_role', 'wps_options', 'wps_identity', 'wps_status_to_js', 'wps_watch_to_js',
                     'wps_error', 'wps_bytes', 'wps_pin_to_js', 'wps_credentials_to_js'):
            code += extract(public, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_options_validate_before_native_actions_and_scrub_on_failure(self):
        cases = [('({})', 1), ('({method:"pin"})', 1), ('({method:"pin",pin:"12345670"})', 1),
                 ('({method:"pin",pin:"00000000"})', 1), ('({pin:"12345670"})', 0),
                 ('({method:"pin",pin:"12345671"})', 0), ('({method:"pin",pin:"1234"})', 0),
                 ('({method:"pin",pin:"1234567\\u0000"})', 0), ('({method:"pin",pin:12345670})', 0),
                 ('({method:"registrar"})', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:3600001})', 0),
                 ('({timeoutMs:1.5})', 0), ('({timeoutMs:"20"})', 0), ('({allowApChannelChange:1})', 0),
                 ('({allowApChannelChange:true,timeoutMs:3600000})', 1),
                 ('({device:{manufacturer:"%s%n",modelName:"",deviceName:"device"}})', 1),
                 ('({device:{manufacturer:"' + 'x' * 64 + '"}})', 1),
                 ('({device:{manufacturer:"' + 'x' * 65 + '"}})', 0),
                 ('({device:{modelName:"' + 'é' * 16 + '"}})', 1),
                 ('({device:{modelName:"' + 'é' * 17 + '"}})', 0),
                 ('({device:{modelNumber:"a\\u0000b"}})', 0), ('({device:{unknown:1}})', 0),
                 ('({device:null})', 0), ('({unknown:1})', 0), ('null', 0), ('[]', 0),
                 ('({get method(){gc();return "pin";},get pin(){gc();return "12345670";},get device(){gc();return {get deviceName(){gc();return "dev";}};}})', 1),
                 ('({method:"pin",pin:"12345670",get device(){gc();throw new Error("sentinel");}})', 0)]
        for value, expected in cases:
            with self.subTest(value=value): run([str(self.binary), 'options', value, str(expected)])

    def test_ap_options_reject_station_controls_and_share_pin_validation(self):
        for value, expected in [('({})', 1), ('({method:"pin",pin:"12345670"})', 1),
                                ('({allowApChannelChange:false})', 0),
                                ('({allowApChannelChange:true})', 0),
                                ('({method:"pin",pin:"12345671"})', 0),
                                ('({timeoutMs:1.5})', 0), ('({device:{unknown:1}})', 0),
                                ('({get method(){gc();return "pin";},get pin(){gc();return "12345670";}})', 1)]:
            with self.subTest(value=value): run([str(self.binary), 'ap-options', value, str(expected)])

    def test_copy_commit_survives_gc_and_allocation_failure(self):
        for mode in ('pin', 'credentials', 'status', 'observation', 'error', 'closed', 'consumed', 'bad-count', 'bad-length'):
            run([str(self.binary), mode, '', '1'])


BOUNDARIES = fixture_text('wifi/provisioning/wps/test_wifi_wps_capture_gc/boundaries.inc')
MAIN = fixture_text('wifi/provisioning/wps/test_wifi_wps_capture_gc/main.inc')
