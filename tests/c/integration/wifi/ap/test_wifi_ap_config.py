"""Production AP capture/validator/readback/result; Wi-Fi phase run pending."""
from tests.support.fixtures import fixture_text
import json
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run


BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_config/boundaries.inc')

MAIN = fixture_text('wifi/ap/test_wifi_ap_config/main.inc')


class WiFiAPConfig(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        inventory = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())
        symbols = inventory['variants']['esp32c5/representative']['symbols']
        # Use recorded public C declarations, including the actual bit widths.
        header = 'components/esp_wifi/include/esp_wifi_types_generic.h::'
        sdk = '\n'.join(symbols[header + n]['declaration'] + ';' for n in (
            'wifi_auth_mode_t', 'wifi_cipher_type_t', 'wifi_sae_pwe_method_t',
            'wifi_pmf_config_t', 'wifi_bss_max_idle_config_t', 'wifi_ap_config_t'))
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        radio = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        ap = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
        body = '#include "cutils.h"\n' + options + sdk + BOUNDARIES
        body += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        body += ''.join(extract(radio, n) for n in ('esp32_mquickjs_wifi_radio_5ghz_channel_bit',
            'esp32_mquickjs_wifi_radio_validate_ap_config', 'esp32_mquickjs_wifi_radio_accept_ap_config',
            'wifi_radio_ap_prestart_config_matches',
            'wifi_radio_ap_reopen_config_matches'))
        body += extract(ap, 'esp32_mquickjs_wifi_parse_ap_config_for_operation')
        body += (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_config_fields.h').read_text()
        capture = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        body += ''.join(extract(capture, name) for name in ['wifi_capture_config_ssid', 'esp32_mquickjs_wifi_ssid_text', 'esp32_mquickjs_wifi_set_ssid_properties', 'esp32_mquickjs_wifi_parse_start_ap_config'])
        body += ''.join(extract(ap, n) for n in ('esp32_mquickjs_wifi_parse_ap_config', 'wifi_ap_auth_name',
            'wifi_ap_cipher_name', 'wifi_ap_pwe_name', 'wifi_ap_result'))
        cls.binaries = {}
        for profile, gcmp, compatible, idle, h2e in [('all', 1, 1, 1, 1), ('limited', 0, 0, 0, 1), ('no-h2e', 1, 0, 1, 0), ('five', 1, 1, 1, 1)]:
            gates = {'CONFIG_SOC_WIFI_SUPPORT_5G': int(profile == 'five'), 'CONFIG_ESP_WIFI_SOFTAP_SUPPORT': 1, 'CONFIG_ESP_WIFI_ENABLE_WPA3_SAE': 1,
                'CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT': 1, 'CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_SOFTAP': 1,
                'CONFIG_SOC_WIFI_GCMP_SUPPORT': gcmp, 'CONFIG_ESP_WIFI_GCMP_SUPPORT': gcmp,
                'CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT': compatible, 'CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT': idle,
                'CONFIG_ESP_WIFI_ENABLE_SAE_H2E': h2e, 'CONFIG_ESP_WIFI_FTM_ENABLE': 1,
                'CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT': 1}
            prefix = ''.join('#define ' + k + ' ' + str(v) + '\n' for k, v in gates.items())
            cls.binaries[profile] = build(cls.temp.name + '/' + profile, prefix + body, MAIN)

    def capture(self, options, expected=True, kind='', profile='all'):
        run([str(self.binaries[profile]), '(' + options + ')', str(int(expected)), kind])

    def test_existing_defaults_and_full_security_capture(self):
        for options in ['{ssid:"test"}', '{ssid:"test",password:"12345678"}',
                        '{ssid:"test",password:"12345678",authMode:"wpa2",pmf:"required",gtkRekeyIntervalSeconds:60,dtimPeriod:2,csaCount:4,beaconIntervalMs:120}',
                        '{ssid:"test",authMode:"owe"}']:
            self.capture(options)

    def test_disconnect_permission_is_boolean_top_level_and_committed_after_capture(self):
        self.capture('{ssid:"new-ap",allowDisconnect:true}', kind='allow-disconnect')
        self.capture('{ssid:"new-ap",allowDisconnect:false}')
        self.capture('{ssid:"new-ap",allowDisconnect:undefined}')
        for value in ('null', '1', '"true"', '{}', '[]'):
            self.capture('{ssid:"new-ap",allowDisconnect:' + value + '}', False)
        self.capture('{ssid:"new-ap",driver:{allowDisconnect:true}}', False)
        self.capture('{ssid:"new-ap",allowDisconnect:true,password:"short"}', False)

    def test_sae_ext_defaults_and_conflicts(self):
        self.capture('{ssid:"test",password:"x",saeExt:true}', kind='ext')
        for extra in [',authMode:"wpa2"', ',pmf:"optional"', ',pairwiseCipher:"ccmp"',
                      ',saePwe:"hunting-and-pecking"', ',wpa3CompatibleMode:true']:
            self.capture('{ssid:"test",password:"12345678",saeExt:true' + extra + '}', False)
        for profile in ['limited', 'no-h2e']:
            self.capture('{ssid:"test",password:"12345678",saeExt:true}', False, 'unsupported', profile)

    def test_explicit_pmf_disable_security_and_exact_readback(self):
        for auth in ('wpa2', 'wpa/wpa2'):
            self.capture('{ssid:"test",password:"12345678",authMode:"' + auth + '",pmf:"disabled"}', kind='pmf-disabled')
        for auth in ('wpa3', 'wpa2/wpa3', 'wpa'):
            self.capture('{ssid:"test",password:"12345678",authMode:"' + auth + '",pmf:"disabled"}', False)
        for options in ('{ssid:"test",pmf:"disabled"}', '{ssid:"test",authMode:"owe",pmf:"disabled"}',
                        '{ssid:"test",password:"12345678",wpa3CompatibleMode:true,pmf:"disabled"}',
                        '{ssid:"test",password:"12345678",pmf:"disabled\\u0000extra"}'):
            self.capture(options, False)

    def test_compatible_mode_and_canonical_readback(self):
        self.capture('{ssid:"test",password:"12345678",wpa3CompatibleMode:true,transitionDisable:true}')
        self.capture('{ssid:"test",password:"12345678",authMode:"wpa3",wpa3CompatibleMode:true}')
        for options in ['{ssid:"test",password:"short",wpa3CompatibleMode:true}',
                        '{ssid:"test",wpa3CompatibleMode:true}',
                        '{ssid:"test",password:"12345678",wpa3CompatibleMode:true,pairwiseCipher:"gcmp"}']:
            self.capture(options, False)
        self.capture('{ssid:"test",password:"12345678",wpa3CompatibleMode:true}', False, 'unsupported', 'limited')

    def test_idle_units_limits_security_and_gate(self):
        self.capture('{ssid:"test",password:"12345678",bssMaxIdlePeriod:10,bssMaxIdleProtectedKeepAlive:true}', kind='idle')
        for value in [0, 10, 65535]:
            self.capture('{ssid:"test",bssMaxIdlePeriod:' + str(value) + '}')
        for value in ['1', '9', '65536', '10.5', 'NaN', 'Infinity', 'true', '"10"']:
            self.capture('{ssid:"test",bssMaxIdlePeriod:' + value + '}', False)
        self.capture('{ssid:"test",bssMaxIdlePeriod:10,bssMaxIdleProtectedKeepAlive:true}', False)
        self.capture('{ssid:"test",password:"12345678",bssMaxIdleProtectedKeepAlive:true}', False)
        self.capture('{ssid:"test",bssMaxIdlePeriod:10}', False, 'unsupported', 'limited')
        self.capture('{ssid:"test",saeExt:false,wpa3CompatibleMode:false,bssMaxIdlePeriod:0,bssMaxIdleProtectedKeepAlive:false}', profile='limited')

    def test_bad_types_unknown_fields_and_secrets_are_cleared(self):
        for key in ['saeExt', 'wpa3CompatibleMode', 'bssMaxIdleProtectedKeepAlive']:
            for value in ['1', 'null', '"true"']:
                self.capture('{ssid:"test",password:"12345678",' + key + ':' + value + '}', False)
        for options in ['{}', '{ssid:""}', '{ssid:"a\\u0000b"}', '{ssid:"test",password:"abc\\u0000defgh"}',
                        '{ssid:"test",authMode:"wpa2"}',
                        '{ssid:"test",password:"12345678",authMode:"open"}']:
            self.capture(options, False)

    def test_nested_driver_merge_units_and_channels(self):
        self.capture('{ssid:"test",password:"12345678",maxConnections:2,driver:{dtimPeriod:3}}', kind='nested')
        self.capture('{ssid:"test",dtimPeriod:3,driver:{password:"12345678",maxConnections:2}}', kind='nested')
        for tu in (100, 300, 3100, 60000):
            self.capture('{ssid:"test",driver:{beaconIntervalTu:' + str(tu) + '}}', kind='tu-' + str(tu))
        for channel in (0, 1, 13, 14):
            self.capture('{ssid:"test",driver:{channel:' + str(channel) + '}}', kind='channel-' + str(channel))
        self.capture('{ssid:"test",driver:{channel:36}}', kind='channel-36', profile='five')
        self.capture('{ssid:"test",driver:{channel:36}}', False, 'unsupported')
        self.capture('{ssid:"test",driver:{password:""}}')
        self.capture('{ssid:"test",password:""}', False)
        self.capture('{ssid:"test",driver:{authMode:"wpa2",password:""}}', False)
        self.capture('{ssid:"test",password:"12345678",driver:{pmf:"disabled"}}', kind='pmf-disabled')
        self.capture('{ssid:"test",password:"x",driver:{saeExt:true}}', kind='ext')
        self.capture('{ssid:"test",password:"x",driver:{saeExt:true}}', False, 'unsupported', 'limited')

    def test_nested_driver_duplicates_bad_values_and_cross_layer_security(self):
        for options in ['{ssid:"test",driver:null}', '{ssid:"test",driver:[]}',
                        '{ssid:"test",driver:{ssid:"other"}}', '{ssid:"test",driver:{beaconIntervalMs:100}}',
                        '{ssid:"test",driver:{unknown:1}}', '{ssid:"test",driver:{driver:{}}}',
                        '{ssid:"test",driver:{"pmf\\u0000suffix":"required"}}',
                        '{ssid:"test",beaconIntervalMs:102.4,driver:{beaconIntervalTu:100}}',
                        '{ssid:"test",channel:1,driver:{channel:1}}',
                        '{ssid:"test",password:"12345678",driver:{password:"12345678"}}',
                        '{ssid:"test",password:"12345678",driver:{authMode:"open"}}',
                        '{ssid:"test",pmf:"optional",driver:{authMode:"wpa3",password:"x"}}']:
            self.capture(options, False)
        for value in ('0', '99', '101', '60001', '65536', '100.5', 'NaN', 'Infinity', '"100"', 'true'):
            self.capture('{ssid:"test",driver:{beaconIntervalTu:' + value + '}}', False)
        for value in ('-1', '15', '35', '178', '256', '1.5', 'NaN', '"1"', 'true'):
            self.capture('{ssid:"test",driver:{channel:' + value + '}}', False)
        self.capture('{ssid:"test",driver:{}}')
        self.capture('{ssid:"test",driver:undefined}')
        self.capture('{ssid:"test",channel:undefined,driver:{channel:13}}', kind='channel-13')
        self.capture('{ssid:"test",channel:1,driver:{channel:undefined}}', kind='channel-1')

    def test_binary_ssid_input_and_exact_start_result(self):
        self.capture('{}', kind='binary-view')
        self.capture('{}', False, kind='closed-view')
        self.capture('{ssid:[255,0,65]}', kind='binary')
        self.capture('{ssid:{length:3,0:255,1:0,2:65},driver:{beaconIntervalTu:300}}', kind='binary')
        self.capture('{ssid:[' + ','.join(['65']*32) + ']}', kind='binary32')
        for source in ('[]', '[256]', '[NaN]', '[1.5]', '[' + ','.join(['65']*33) + ']'):
            self.capture('{ssid:' + source + '}', False)
