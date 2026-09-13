"""Production full Station capture in MQuickJS; Wi-Fi phase execution pending."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
from tests.c.integration.wifi.station.test_wifi_station_rssi_preference import SDK

CONSTANTS = fixture_text('wifi/station/test_wifi_station_phy_security/constants.inc')

MAIN = fixture_text('wifi/station/test_wifi_station_phy_security/main.inc')


class WiFiStationPhySecurity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_future.c').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        names = ('wifi_is_object', 'wifi_string_equals', 'wifi_validate_option_keys', 'wifi_to_integer',
                 'wifi_parse_bssid', 'wifi_parse_auth_mode', 'wifi_parse_station_phy',
                 'wifi_parse_station_extensions', 'esp32_mquickjs_wifi_parse_station_config_for_operation',
                 'esp32_mquickjs_wifi_parse_station_config')
        radio = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        sdk = SDK.replace('static JSValue get_property_with_fault(',
            'static JSValue *tracked_input;static int input_driver_reads,input_password_reads;\nstatic JSValue get_property_with_fault(')
        sdk = sdk.replace('    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;',
            '    if(tracked_input && object==*tracked_input) { if(!strcmp(key,"driver"))input_driver_reads++;if(!strcmp(key,"password"))input_password_reads++; }\n    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;')
        fields = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_config_fields.h').read_text()
        config_source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        body = options + sdk + CONSTANTS + zero + fields + extract(radio, 'esp32_mquickjs_wifi_radio_pmf_disable_allowed') + ''.join(extract(source, n) for n in names)
        body += extract(config_source, 'wifi_capture_config_ssid')
        body += extract(config_source, 'esp32_mquickjs_wifi_parse_connect_config')
        cls.binaries = {}
        for profile, he, vht, security in [('all', 1, 1, 1), ('none', 0, 0, 0), ('he', 1, 0, 1)]:
            definitions = {'CONFIG_SOC_WIFI_HE_SUPPORT': he, 'CONFIG_SOC_WIFI_SUPPORT_5G': vht,
                'CONFIG_ESP_WIFI_ENABLE_WPA3_SAE': security, 'CONFIG_ESP_WIFI_ENABLE_SAE_H2E': security,
                'CONFIG_ESP_WIFI_ENABLE_SAE_PK': security, 'CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT': security,
                'CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA': security}
            prefix = ''.join('#define ' + key + ' ' + str(value) + '\n' for key, value in definitions.items())
            cls.binaries[profile] = build(cls.temp.name + '/' + profile, prefix + body, MAIN)

    def capture(self, options, expected=True, kind='', profile='all', ssid='"capture-test"'):
        run([str(self.binaries[profile]), '(' + options + ')', str(int(expected)), kind, ssid])

    def test_configure_and_connect_pmf_disabled_security(self):
        self.capture('{pmf:"disabled",disableWpa3CompatibleMode:true}', kind='configure-pmf')
        for extra in ('', ',minimumAuthMode:"wpa3"', ',minimumAuthMode:"wpa2/wpa3"', ',oweEnabled:true', ',saePkMode:"only"'):
            options = '{pmf:"disabled"' + (',disableWpa3CompatibleMode:true' if extra else '') + extra + '}'
            self.capture(options, False, kind='configure-invalid')
        self.capture('{pmf:"disabled",disableWpa3CompatibleMode:true}', kind='connect-pmf')
        self.capture('{minimumAuthMode:"wpa3",pmf:"optional"}', False, kind='configure-invalid')
        self.capture('{minimumAuthMode:"wpa3",pmf:"optional"}', False)
        self.capture('{pmf:"disabled\\u0000extra",disableWpa3CompatibleMode:true}', False, kind='configure-invalid')

    def test_phy_bits_and_dcm_defaults(self):
        self.capture('{heDcmSet:true}', kind='he-defaults')
        self.capture('{heDcmSet:true,heDcmMaxConstellationTx:0,heDcmMaxConstellationRx:2,heMcs9Enabled:true}', kind='he-values')
        self.capture('{vhtSuBeamformeeDisabled:true,vhtMuBeamformeeDisabled:true,vhtMcs8Enabled:true}', kind='vht')
        for key in ['heSuBeamformeeDisabled', 'heTrigSuBeamformingFeedbackDisabled',
                    'heTrigMuBeamformingPartialFeedbackDisabled', 'heTrigCqiFeedbackDisabled']:
            self.capture('{' + key + ':true}', profile='he')

    def test_phy_types_ranges_dependencies_and_target_gates(self):
        for options in ['{heDcmMaxConstellationTx:0}', '{heDcmSet:false,heDcmMaxConstellationRx:1}',
                        '{heDcmSet:1}', '{heMcs9Enabled:"true"}', '{vhtMcs8Enabled:0}', '{heMcs9:true}']:
            self.capture(options, False)
        for number in ['-1', '4', '0.5', 'NaN', 'Infinity', 'true', '"2"']:
            self.capture('{heDcmSet:true,heDcmMaxConstellationTx:' + number + '}', False)
        for key, profile in [('heDcmSet', 'none'), ('heMcs9Enabled', 'none'), ('vhtMcs8Enabled', 'he')]:
            for value in ['true', 'false']:
                self.capture('{' + key + ':' + value + '}', False, 'unsupported:' + key, profile)

    def test_pk_only_cannot_take_a_weaker_authentication_path(self):
        self.capture('{password:"capture-only",saePkMode:"only"}', kind='only')
        self.capture('{password:"capture-only",saePkMode:"only",minimumAuthMode:"wpa3",pmf:"required",saePwe:"hash-to-element"}', kind='only')
        for extra in [',minimumAuthMode:"wpa2"', ',minimumAuthMode:"wpa2/wpa3"', ',pmf:"optional"',
                      ',saePwe:"hunting-and-pecking"', ',oweEnabled:true']:
            self.capture('{password:"capture-only",saePkMode:"only"' + extra + '}', False)
        self.capture('{saePkMode:"only"}', False)
        self.capture('{password:"' + 'a' * 64 + '",saePkMode:"only"}', False)
        self.capture('{saePkMode:"only\\u0000suffix"}', False)

    def test_security_gates_and_explicit_disabled_modes(self):
        self.capture('{saePkMode:"disabled"}', kind='disabled', profile='none')
        for mode in ['automatic', 'only']:
            self.capture('{password:"capture-only",saePkMode:"' + mode + '"}', False, 'unsupported:saePkMode', 'none')
        for key in ['transitionDisable', 'disableWpa3CompatibleMode']:
            self.capture('{' + key + ':true}', False, 'unsupported:' + key, 'none')
            self.capture('{' + key + ':false}', profile='none')
        self.capture('{password:"capture-only",transitionDisable:true,disableWpa3CompatibleMode:true}', kind='security-flags')

    def test_nested_driver_merges_before_shared_security_validation(self):
        self.capture('{password:"nested-secret",timeoutMs:4321,driver:{listenInterval:77,minimumRssi:-70}}', kind='nested')
        self.capture('{listenInterval:77,minimumRssi:-70,timeoutMs:4321,driver:{password:"nested-secret"}}', kind='nested')
        self.capture('{driver:{pmf:"disabled",disableWpa3CompatibleMode:true}}', kind='connect-pmf')
        self.capture('{driver:{heDcmSet:true}}', kind='he-defaults', profile='he')
        self.capture('{driver:{heDcmSet:true}}', False, kind='unsupported:heDcmSet', profile='none')
        self.capture('{pmf:"optional",driver:{minimumAuthMode:"wpa3"}}', False)
        self.capture('{minimumAuthMode:"wpa3",driver:{pmf:"optional"}}', False)
        self.capture('{password:"nested-secret",driver:{oweEnabled:true}}', False)
        self.capture('{driver:{minimumAuthMode:"enterprise"}}', False)

    def test_nested_driver_rejects_duplicate_and_unknown_fields(self):
        for options in ['{driver:null}', '{driver:[]}', '{driver:1}',
                        '{driver:{ssid:"other"}}', '{driver:{timeoutMs:1}}',
                        '{driver:{unknown:1}}', '{driver:{driver:{}}}',
                        '{driver:{"pmf\\u0000suffix":"required"}}',
                        '{password:"x",driver:{password:"x"}}',
                        '{pmf:"required",driver:{pmf:"optional"}}',
                        '{minimumRssi:0,driver:{minimumRssi:0}}']:
            self.capture(options, False)
        self.capture('{driver:{}}')
        self.capture('{driver:undefined}')
        self.capture('{pmf:undefined,driver:{pmf:"required"}}')
        self.capture('{pmf:"required",driver:{pmf:undefined}}')

    def test_binary_ssid_sources_copy_bytes_and_reject_nul(self):
        self.capture('0', kind='no-options', ssid='[255,65]')
        for source in ('[255,65]', '{length:2,0:255,1:65}', 'view'):
            self.capture('{}', kind='binary', ssid='(' + source + ')' if source.startswith('{') else source)
        self.capture('{}', kind='binary32', ssid='[' + ','.join(['65']*32) + ']')
        for source in ('[]', '[65,0,66]', '[256]', '[1.5]', '[NaN]', 'closed-view',
                       '[' + ','.join(['65']*33) + ']'):
            self.capture('{}', False, ssid=source)
