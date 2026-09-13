"""Actual CSI options, requested-result conversion and pool allocator in MQuickJS.

Execution is deferred to the Wi-Fi phase gate. Radio/SDK callbacks are not
simulated by this fixture; it verifies input capture and native storage sizing.
"""
from tests.support.fixtures import fixture_text
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
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
        body += unit(base.parent / 'wifi_common/esp32_mquickjs_wifi_frame_type.c')
        body += unit(base.parent / 'wifi_common/esp32_mquickjs_wifi_frame_filter.c')
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

        he_body = body.replace('wifi_csi_target_is_he(void){return false;}',
                               'wifi_csi_target_is_he(void){return true;}')
        he_body = he_body.replace('wifi_csi_target_supports_vht(void){return false;}',
                                 'wifi_csi_target_supports_vht(void){return true;}')
        he_body = he_body.replace('wifi_csi_target_supports_lltf_bit_mode(void){return false;}',
                                 'wifi_csi_target_supports_lltf_bit_mode(void){return true;}')
        cls.he_binary = build(cls.temp.name + '/he',
            '#define CONFIG_ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS 1\n' + he_body, MAIN)

    def test_he_requested_configuration_round_trip(self):
        for capture in ('schema:"wifi-csi-he/1"',
                        'schema:"wifi-csi-he/1",enableLegacy:false,forceLegacyLtf:true,ht40:false,vht:false,heStbcLtf:"alternate",valueScale:3,lltfBits:8'):
            for fields in ('', ',filter:{frames:[],maximumRateHz:0}',
                           ',source:{mode:"promiscuous",channel:6},filter:{sourceMac:"02:00:00:00:00:01",destinationMac:"ff:ff:ff:ff:ff:ff",bssid:"02:00:00:00:00:02",minimumRssi:-75,types:["data"],subtypes:[0],frames:[{type:2,subtype:0}],maximumRateHz:500},packet:{content:"full",snapLength:512,required:true}'):
                value = '({capture:{' + capture + '}' + fields + '})'
                run([str(self.he_binary), value, '1', '8', '4'])

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
        for fields in ('filter:{bssid:"02:00:00:00:00:01",types:["data"],subtypes:[0,8]}',
                       'filter:{types:[],subtypes:[]}',
                       'filter:{types:["management","control","data","misc"]}'):
            self.capture(fields)
        for fields in ('filter:{types:["unknown"]}', 'filter:{bssid:"02:00:00:00:00:01\\x00suffix"}',
                       'filter:{types:["data\\x00"]}', 'filter:{types:["data","data"]}',
                       'filter:{types:null}', 'filter:{types:"data"}', 'filter:{types:[{}]}',
                       'filter:{subtypes:[-1]}', 'filter:{subtypes:[16]}',
                       'filter:{subtypes:[0.5]}', 'filter:{subtypes:[4294967296]}',
                       'filter:{subtypes:["1"]}', 'filter:{subtypes:[1,1]}'):
            self.capture(fields, False)

    def test_exact_frame_pairs(self):
        self.capture('filter:{frames:[{type:0,subtype:8,name:"beacon"},{type:2,subtype:0,name:"data"}]}')
        self.capture('filter:{frames:[]}')
        self.capture('filter:{types:["management","data"],subtypes:[0,8],frames:[{type:0,subtype:8},{type:2,subtype:0}],maximumRateHz:0}')
        self.capture('powerSavePolicy:"preserve"', False)
        self.capture('filter:{frameTypes:["data"]}', False)
        self.capture('filter:{frameSubtypes:[0]}', False)
        self.capture('filter:{frames:[{type:3,subtype:15,name:null}]}')
        self.capture('filter:{frames:(function(){var a=[];for(var t=0;t<4;t++)for(var s=0;s<16;s++)a.push({type:t,subtype:s});return a;})()}')
        self.capture('filter:{frames:(function(){var a=0,b=0,c=0;return [{get type(){if(++a!==1)throw new Error("twice");return 0;},get subtype(){if(++b!==1)throw new Error("twice");return 8;},get name(){if(++c!==1)throw new Error("twice");return "beacon";}}];})()}')
        for frames in ('null', '{}', '[null]', 'new Array(65)',
                       '[{type:4,subtype:0}]', '[{type:0,subtype:16}]',
                       '[{type:0}]', '[{subtype:0}]', '[{type:"0",subtype:0}]',
                       '[{type:0.5,subtype:0}]', '[{type:0,subtype:0,name:"beacon"}]',
                       '[{type:0,subtype:8,name:null}]', '[{type:3,subtype:0,name:"beacon"}]',
                       '[{type:0,subtype:8,extra:true}]', '[{type:0,subtype:8},{type:0,subtype:8}]'):
            self.capture('filter:{frames:'+frames+'}', False)

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
