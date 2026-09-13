"""Actual outer/raw configuration capture + VM/ByteView + Radio pure validators.

Only the existing shared personal-security parser is injected; its independent
production fixtures cover policy. This suite focuses on nested controls capture,
strict input, native output zeroing, and GC/allocation ownership. Run deferred.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_capture import BOUNDARIES as RAW_BOUNDARIES

MAIN = fixture_text('wifi/config/test_wifi_configuration_capture/main.inc')


class WiFiConfigurationCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        radio_header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h').read_text()
        wifi_header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi.h').read_text()
        structures = '\n'.join(structure(radio_header, n) for n in (
            'esp32_mquickjs_wifi_radio_config_controls_t', 'esp32_mquickjs_wifi_radio_start_controls_t',
            'esp32_mquickjs_wifi_radio_configuration_selection_t'))
        structures += structure(wifi_header, 'esp32_mquickjs_wifi_configuration_t')
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        radio = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        validators = ''.join(extract(radio, n) for n in ('esp32_mquickjs_wifi_radio_5ghz_channel_bit',
            'wifi_radio_validate_protocol', 'esp32_mquickjs_wifi_radio_validate_config_controls',
            'esp32_mquickjs_wifi_radio_validate_start_controls'))
        capture = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        # Connect's legacy argument adapter is outside configuration capture;
        # its Station policy is covered by the connection/security fixtures.
        capture = capture.replace(extract(capture, 'esp32_mquickjs_wifi_parse_connect_config'), '')
        capture = '\n'.join(line for line in capture.splitlines() if not line.startswith('#include')) + '\n'
        fields = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_config_fields.h').read_text()
        boundaries = RAW_BOUNDARIES[RAW_BOUNDARIES.index('static int policy_calls'):]
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        cls.binaries = {}
        for profile, target, ap, he, five in [('all', 'esp32c5', 1, 1, 1), ('c3', 'esp32c3', 1, 0, 0),
                                            ('no-ap', 'esp32c3', 0, 0, 0)]:
            defines = (f'#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n'
                       f'#define CONFIG_SOC_WIFI_HE_SUPPORT {he}\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n'
                       '#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100\n#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000\n')
            body = '#include "cutils.h"\n' + PRELUDE + defines + sdk_types(target + '/representative') + structures
            body += options + zero + validators + boundaries + fields + capture
            cls.binaries[profile] = build(cls.temp.name + '/' + profile, body, MAIN)

    def capture(self, options, expected=True, kind='', profile='all'):
        run([str(self.binaries[profile]), '(' + options + ')', str(int(expected)), kind])

    def test_presence_and_complete_controls(self):
        self.capture('{}', kind='omitted')
        self.capture('{mode:"station",storage:"ram",start:false,allowDisconnect:false}', kind='explicit')
        self.capture('''{mode:"apsta",storage:"flash",start:true,allowDisconnect:true,
          station:{ssid:"station"},accessPoint:{ssid:[0,128,255]},country:{code:"TW",policy:"manual"},
          protocols:{station:{ghz2:["11b","11g","11n"],ghz5:["11a","11n","11ac"]}},
          bandwidths:{station:{ghz2MHz:40,ghz5MHz:20}},powerSave:"none",txPowerDbm:18.25}''', kind='combined')
        self.capture('{country:{code:"TW",policy:"manual",startChannel:1,channelCount:13,environment:"indoor"}}', kind='country')
        self.capture('{protocols:{"access-point":{ghz2:["11b","11g","11n"]}},bandwidths:{"access-point":{ghz2MHz:40}}}')
        self.capture('{country:"01",powerSave:"maximum",txPowerDbm:2}')

    def test_nested_input_ranges_conflicts_and_full_zeroing(self):
        invalid = ['null', '[]', 'true', '{unknown:1}', '{start:1}', '{allowDisconnect:"yes"}',
                   '{country:"tw"}', '{country:"TWN"}', '{country:{code:"TW",startChannel:1}}',
                   '{country:{code:"TW",channelCount:13}}', '{country:{code:"TW",environment:null}}',
                   '{country:{code:"TW",startChannel:14,channelCount:2}}',
                   '{country:{code:"TW",startChannel:1,channelCount:13,maxTxPowerDbm:20}}',
                   '{country:{code:"TW",policy:"auto",startChannel:1,channelCount:13,ghz5ChannelMask:2}}',
                   '{protocols:{}}', '{protocols:{station:{}}}', '{protocols:{ap:{ghz2:["11b"]}}}',
                   '{protocols:{station:{ghz2:[]}}}', '{protocols:{station:{ghz2:["11b","11b"]}}}',
                   '{protocols:{station:{ghz2:["11n"]}}}', '{protocols:{station:{ghz2:["11a"]}}}',
                   '{protocols:{station:{ghz2:{length:1,0:"11b"}}}}',
                   '{bandwidths:{station:{ghz2MHz:21}}}', '{bandwidths:{station:{ghz2MHz:"20"}}}',
                   '{protocols:{station:{ghz5:["11a","11n","11ac"]}},bandwidths:{station:{ghz5MHz:40}}}',
                   '{mode:"station",accessPoint:{ssid:"x"}}', '{mode:"ap",station:{ssid:"x"}}',
                   '{mode:"ap",powerSave:"none"}', '{mode:"station",protocols:{"access-point":{ghz2:["11b"]}}}',
                   '{start:false,txPowerDbm:18}', '{txPowerDbm:1.99}', '{txPowerDbm:20.25}',
                   '{txPowerDbm:18.1}', '{txPowerDbm:NaN}', '{txPowerDbm:Infinity}', '{txPowerDbm:"18"}',
                   '{station:{ssid:"s"},accessPoint:{ssid:"ap",unknown:1}}']
        for options in invalid:
            with self.subTest(options=options):
                self.capture(options, False)

    def test_strict_enum_rejects_embedded_nul(self):
        for options in [r'{mode:"station\u0000x"}', r'{storage:"ram\u0000x"}',
                        r'{country:{code:"TW",policy:"manual\u0000x"}}',
                        r'{powerSave:"none\u0000x"}', r'{protocols:{station:{ghz2:["11b\u0000x"]}}}',
                        r'{"mode\u0000x":"station"}', r'{country:{code:"TW","policy\u0000x":"manual"}}',
                        r'{protocols:{station:{"ghz2\u0000x":["11b"]}}}' ]:
            self.capture(options, False)

    def test_real_target_gates(self):
        self.capture('{protocols:{station:{ghz2:["11b","11g","11n"]}}}', profile='c3')
        self.capture('{mode:"station",start:false}', profile='no-ap')
        for options, profile in [('{mode:"ap"}', 'no-ap'), ('{accessPoint:{ssid:"a"}}', 'no-ap'),
                                 ('{protocols:{"access-point":{ghz2:["11b"]}}}', 'no-ap'),
                                 ('{protocols:{station:{ghz5:["11a"]}}}', 'c3'),
                                 ('{protocols:{station:{ghz2:["11b","11g","11n","11ax"]}}}', 'c3'),
                                 ('{country:{code:"TW",startChannel:1,channelCount:13,ghz5ChannelMask:0}}', 'c3')]:
            self.capture(options, False, profile=profile)
