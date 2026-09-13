"""Actual CSI options, requested-result conversion and pool allocator in MQuickJS.

Execution is deferred to the Wi-Fi phase gate. Radio/SDK callbacks are not
simulated by this fixture; it verifies input capture and native storage sizing.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run


class WiFiCsiOptionsGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        base = ROOT / 'components/esp32_mquickjs/src/modules/wifi_csi'
        source = (base / 'esp32_mquickjs_wifi_csi.c').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        a = source.index('typedef enum {\n    WIFI_CSI_SOURCE_ASSOCIATED')
        b = source.index('} wifi_csi_options_t;', a) + len('} wifi_csi_options_t;')
        body = BOUNDARIES + source[a:b] + '\n' + options
        body += (base / 'esp32_mquickjs_wifi_csi_target_config.c').read_text()
        body += (CORE / 'esp32_mquickjs_native_pool.c').read_text()
        body += (CORE / 'esp32_mquickjs_native_lease.c').read_text()
        body += (base / 'esp32_mquickjs_wifi_csi_resources.c').read_text()
        body += (base / 'esp32_mquickjs_wifi_csi_packet.c').read_text()
        body += (base.parent / 'wifi_common/esp32_mquickjs_wifi_rx.c').read_text()
        body += ''.join(extract(source, n) for n in (
            'wifi_csi_source_name', 'wifi_csi_format_mac', 'wifi_csi_hex_digit',
            'wifi_csi_parse_mac_text', 'wifi_csi_string_equals', 'wifi_csi_get_optional_bool',
            'wifi_csi_default_options', 'wifi_csi_parse_mac_values_rooted', 'wifi_csi_parse_mac_values',
            'wifi_csi_packet_type_name', 'wifi_csi_parse_frame_filter', 'wifi_csi_frame_filter_to_js',
            'wifi_csi_parse_filter_rooted', 'wifi_csi_parse_filter',
            'wifi_csi_parse_legacy_capture_rooted', 'wifi_csi_parse_legacy_capture',
            'wifi_csi_parse_he_capture_rooted', 'wifi_csi_parse_he_capture',
            'wifi_csi_parse_source', 'wifi_csi_parse_buffering',
            'wifi_csi_packet_mode_name', 'wifi_csi_parse_packet', 'wifi_csi_packet_options_to_js',
            'wifi_csi_parse_open_options_rooted', 'wifi_csi_parse_open_options',
            'wifi_csi_mac_array', 'wifi_csi_capture_to_js', 'wifi_csi_source_to_js',
            'wifi_csi_requested_to_js'))
        cls.binaries = [build(cls.temp.name + '/' + str(enabled),
            '#define CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS ' + str(enabled) + '\n' + body, MAIN)
            for enabled in (0, 1)]

    def capture(self, fields, ok=True, pool=8, queue=4, enabled=1):
        value = '({capture:{schema:"wifi-csi-legacy/1"}' + (',' + fields if fields else '') + '})'
        run([str(self.binaries[enabled]), value, str(int(ok)), str(pool), str(queue)])

    def test_source_and_actual_capacity_with_nth_failure_and_moving_gc(self):
        self.capture('')
        self.capture('source:{mode:"associated"},buffering:{poolCapacity:1}', pool=1, queue=1)
        self.capture('buffering:{poolCapacity:3,queueCapacity:2,overflow:"drop-newest"}', pool=3, queue=2)
        self.capture('source:{mode:"promiscuous",channel:6},buffering:{poolCapacity:8,queueCapacity:8}', pool=8, queue=8)
        self.capture('source:{mode:"associated"}', enabled=0)
        self.capture('source:{mode:"promiscuous"}', False, enabled=0)
        for fields in (
            'source:{}', 'source:{mode:"associated",channel:undefined}',
            'source:{mode:"associated",channel:6}', 'source:{mode:"promiscuous",channel:0}',
            'source:{mode:"promiscuous",channel:36}',
            'buffering:{poolCapacity:0}', 'buffering:{poolCapacity:9}',
            'buffering:{poolCapacity:4294967296}', 'buffering:{poolCapacity:1.5}',
            'buffering:{poolCapacity:2,queueCapacity:3}', 'buffering:{queueCapacity:0}',
            'buffering:{queueCapacity:"2"}', 'buffering:{overflow:"drop-oldest"}',
        ):
            self.capture(fields, False)

    def test_header_filters_rooting_and_strict_arrays(self):
        for fields in ('filter:{bssid:"02:00:00:00:00:01",frameTypes:["data"],frameSubtypes:[0,8]}',
                       'filter:{frameTypes:[],frameSubtypes:[]}', 'filter:{frameTypes:["unknown"]}',
                       'filter:{frameTypes:["management","control","data","misc","unknown"]}'):
            self.capture(fields)
        for fields in ('filter:{bssid:"02:00:00:00:00:01\\x00suffix"}',
                       'filter:{frameTypes:["data\\x00"]}', 'filter:{frameTypes:["data","data"]}',
                       'filter:{frameTypes:null}', 'filter:{frameTypes:"data"}', 'filter:{frameTypes:[{}]}',
                       'filter:{frameSubtypes:[-1]}', 'filter:{frameSubtypes:[16]}',
                       'filter:{frameSubtypes:[0.5]}', 'filter:{frameSubtypes:[4294967296]}',
                       'filter:{frameSubtypes:["1"]}', 'filter:{frameSubtypes:[1,1]}'):
            self.capture(fields, False)

    def test_packet_options_rooting_validation_and_native_reservation(self):
        for fields in ('packet:{content:"none"}', 'packet:{content:"header",snapLength:36}',
                       'packet:{content:"full",snapLength:16384,required:true}', 'packet:{requireComplete:true}'):
            self.capture(fields)
        for fields in ('packet:{content:"header",snapLength:35}', 'packet:{content:"full",snapLength:16385}',
                       'packet:{required:true}', 'packet:{content:"header",requireComplete:true}',
                       'packet:{content:"none",requireComplete:true}', 'packet:{content:"other"}',
                       'packet:{snapLength:4294967296}', 'packet:{content:"full\\x00suffix"}', 'packet:{required:1}', 'packet:{extra:true}'):
            self.capture(fields, False)


BOUNDARIES = fixture_text('wifi/csi/test_wifi_csi_options_gc/boundaries.inc')

MAIN = fixture_text('wifi/csi/test_wifi_csi_options_gc/main.inc')
