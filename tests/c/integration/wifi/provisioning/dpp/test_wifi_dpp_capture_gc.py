"""Deferred real-VM DPP converters: Nth allocation failure and moving GC.

Links production Session storage/copy/commit and public converters. Injects
received rows at the native result boundary; no provisioning/crypto/RF claim.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.provisioning.dpp.test_wifi_dpp_session import ROOT, TYPES, headers, unit
from tests.c.integration.wifi.provisioning.dpp.test_wifi_dpp_connection import ROW
from tests.c.integration.wifi.config.test_wifi_ssid_result import BOUNDARIES as LINK_BOUNDARIES
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class DppCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = (ROOT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp.c').read_text()
        native = (ROOT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_session.c').read_text()
        code = TYPES.replace('typedef struct {uint8_t ssid[32],ssid_len,key[128];} esp_dpp_config_data_t;', ROW)
        code += headers() + BOUNDARIES
        link_boundaries = LINK_BOUNDARIES.replace('#define ESP_ERR_INVALID_RESPONSE -7\n', '')
        link_boundaries = re.sub(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_link_snapshot_t;', '', link_boundaries)
        code += link_boundaries
        a = native.index('struct esp32_mquickjs_wifi_dpp_session {')
        b = native.index('static void dpp_session_close_locked', a)
        code += native[a:b]
        for name in ('dpp_session_close_locked', 'esp32_mquickjs_wifi_dpp_open_runtime',
                     'esp32_mquickjs_wifi_dpp_session_create', 'esp32_mquickjs_wifi_dpp_session_release',
                     'esp32_mquickjs_wifi_dpp_session_close', 'esp32_mquickjs_wifi_dpp_session_status',
                     'esp32_mquickjs_wifi_dpp_session_uri', 'esp32_mquickjs_wifi_dpp_session_config',
                     'esp32_mquickjs_wifi_dpp_session_configs_commit',
                     'esp32_mquickjs_wifi_dpp_session_connection_result'):
            code += extract(native, name)
        wifi_base = ROOT / 'src/modules/wifi'
        config = (wifi_base / 'esp32_mquickjs_wifi_config.c').read_text()
        wifi = (wifi_base / 'esp32_mquickjs_wifi.c').read_text()
        code += ''.join(extract(config, name) for name in (
            'esp32_mquickjs_wifi_ssid_text', 'esp32_mquickjs_wifi_set_ssid_properties'))
        code += ''.join(extract(wifi, name) for name in (
            'wifi_negotiated_phy_name', 'wifi_link_snapshot_valid', 'wifi_set_link_properties',
            'esp32_mquickjs_wifi_make_connect_result'))
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        code += 'typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define DPP_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        for name in ('dpp_identity', 'dpp_status_to_js', 'dpp_watch_to_js', 'dpp_error',
                     'dpp_bytes', 'dpp_uri_to_js', 'dpp_configurations_to_js'):
            code += extract(public, name)
        code += re.search(r'typedef enum \{[^}]*\} dpp_operation_t;', public).group(0)
        a = public.index('struct esp32_mquickjs_future_driver_state {')
        code += public[a:public.index('\n};', a) + 3]
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += extract(public, 'dpp_operation_name') + extract(public, 'dpp_finish')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_transactional_results_gc_and_every_conversion_allocation(self):
        for mode in ('uri', 'configurations', 'status', 'observation', 'error', 'closed',
                     'consumed', 'bad-count', 'bad-length', 'bad-connector',
                     'connection', 'disconnected-result'):
            with self.subTest(mode=mode):
                run([str(self.binary), mode])


BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_capture_gc/boundaries.inc')
MAIN = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_capture_gc/main.inc')
