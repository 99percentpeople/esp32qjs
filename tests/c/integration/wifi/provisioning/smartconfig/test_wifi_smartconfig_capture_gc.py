"""Deferred real MQuickJS option/credential/status conversion with GC/Nth OOM.

Production Session ownership, copy/commit and public converters are linked.
Injected producer bytes are not RF or full Future-core execution evidence.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.provisioning.smartconfig.test_wifi_smartconfig_session import COMPONENT, TYPES, headers, unit
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class SmartConfigCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig.c').read_text()
        native = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_session.c').read_text()
        decoder = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_decoder.c').read_text()
        code = TYPES + headers() + BOUNDARIES
        a = native.index('struct esp32_mquickjs_wifi_smartconfig_session {')
        b = native.index('static void sc_session_close_locked', a)
        code += native[a:b]
        code += extract(decoder, 'esp32_mquickjs_wifi_smartconfig_decoder_validate_options')
        for name in ('esp32_mquickjs_wifi_smartconfig_connection_validate_options',
                     'sc_session_close_locked', 'esp32_mquickjs_wifi_smartconfig_open_runtime',
                     'esp32_mquickjs_wifi_smartconfig_session_create', 'esp32_mquickjs_wifi_smartconfig_session_retain',
                     'esp32_mquickjs_wifi_smartconfig_session_release', 'esp32_mquickjs_wifi_smartconfig_session_close',
                     'esp32_mquickjs_wifi_smartconfig_session_status', 'esp32_mquickjs_wifi_smartconfig_session_credentials'):
            code += extract(native, name)
        code += re.search(r'typedef struct \{[^}]*\} sc_options_t;', public).group(0)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        code += 'typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define SC_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        for name in ('sc_options', 'sc_identity', 'sc_status_to_js', 'sc_watch_to_js', 'sc_error', 'sc_bytes', 'sc_credentials_to_js'):
            code += extract(public, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_options_reject_invalid_inputs_and_clear_failed_key_copies(self):
        cases = [('({})', 1), ('({protocol:"esptouch-v2",aesKey:"1234567890abcdef"})', 1),
                 ('({autoConnect:true})', 1),
                 ('({autoConnect:true,minimumAuthMode:"wpa3-psk"})', 1),
                 ('({autoConnect:true,allowOpenNetwork:true})', 1),
                 ('({autoConnect:true,minimumAuthMode:"wpa3-psk",pmf:"optional"})', 0),
                 ('({autoConnect:true,allowOpenNetwork:true,pmf:"required"})', 0),
                 ('({autoConnect:true,connectionTimeoutMs:0})', 0),
                 ('({autoConnect:true,connectionTimeoutMs:3600001})', 0),
                 ('({autoConnect:true,minimumAuthMode:"open"})', 0),
                 ('({autoConnect:false,connectionTimeoutMs:10})', 0),
                 ('({allowOpenNetwork:false})', 0),
                 ('({protocol:"airkiss",aesKey:"1234567890abcdef"})', 0),
                 ('({protocol:"esptouch-v2",aesKey:"1234567890abcde\\u0000"})', 0),
                 ('({protocol:"esptouch-v2",aesKey:"short"})', 0),
                 ('({protocol:"esptouch-v2",aesKey:"1234567890abcdeé"})', 0),
                 ('({protocol:"bad"})', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:3600001})', 0),
                 ('({timeoutMs:1.5})', 0), ('({timeoutMs:"20"})', 0), ('({fastMode:1})', 0),
                 ('({channelTimeoutSeconds:14})', 0), ('({channelTimeoutSeconds:256})', 0),
                 ('({allowApChannelChange:true,channelTimeoutSeconds:255,timeoutMs:3600000})', 1),
                 ('({unknown:1})', 0), ('null', 0), ('[]', 0),
                 ('({get protocol(){gc();return "esptouch-v2";},get aesKey(){gc();return "1234567890abcdef";}})', 1),
                 ('({get aesKey(){gc();throw new Error("sentinel");}})', 0)]
        for value, expected in cases:
            with self.subTest(value=value): run([str(self.binary), 'options', value, str(expected)])

    def test_credential_transfer_retry_exact_bytes_and_metadata_only_status(self):
        for mode in ('credentials', 'credentials-empty-custom', 'credentials-airkiss', 'status', 'observation', 'error', 'closed', 'consumed'):
            run([str(self.binary), mode, '', '1'])


BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_capture_gc/boundaries.inc')
MAIN = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_capture_gc/main.inc')
