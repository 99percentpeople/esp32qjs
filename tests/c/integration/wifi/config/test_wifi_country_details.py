"""Deferred country-control regressions using production Radio and VM helpers.

SDK storage/callback boundaries are injected. These fixtures do not establish
country-specific SDK normalization, flash durability, or RF behavior.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest

from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types, structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, ROOT, build, extract, run

COMPONENT = ROOT / 'components/esp32_mquickjs'
RADIO = COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'
HEADER = COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h'


def types(target):
    header = HEADER.read_text()
    out = sdk_types(target + '/representative', ('wifi_ap_record_t',))
    for name in ('esp32_mquickjs_wifi_radio_config_controls_t',
                 'esp32_mquickjs_wifi_radio_config_result_t',
                 'esp32_mquickjs_wifi_radio_mutation_t'):
        out += structure(header, name) + '\n'
    return out


def gates(target):
    five = int(target == 'esp32c5')
    return (f'\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n'
            f'#define CONFIG_SOC_WIFI_HE_SUPPORT {five}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n')


def validators(source):
    return ''.join(extract(source, name) for name in (
        'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_validate_protocol',
        'esp32_mquickjs_wifi_radio_validate_config_controls'))


NATIVE_BOUNDARY = fixture_text('wifi/config/test_wifi_country_details/native_boundary.inc')

NATIVE_RESET = fixture_text('wifi/config/test_wifi_country_details/native_reset.inc')

DETAILS_MAIN = fixture_text('wifi/config/test_wifi_country_details/details_main.inc')

CODE_MAIN = fixture_text('wifi/config/test_wifi_country_details/code_main.inc')


class WiFiCountryDetails(unittest.TestCase):
    def native(self, main):
        source, header = RADIO.read_text(), HEADER.read_text()
        client = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', header).group(0)
        local = client + structure(header, 'esp32_mquickjs_wifi_radio_lease_t')
        local += structure(source, 'wifi_radio_controls_snapshot_t')
        helpers = validators(source) + ''.join(extract(source, name) for name in (
            'wifi_radio_country_valid', 'wifi_radio_country_equal', 'wifi_radio_read_phy',
            'wifi_radio_phy_equal', 'wifi_radio_write_phy', 'wifi_radio_snapshot_controls',
            'wifi_radio_apply_country', 'wifi_radio_restore_controls', 'wifi_radio_lease_valid',
            'wifi_radio_configuration_owner_locked', 'esp32_mquickjs_wifi_radio_set_country_details',
            'esp32_mquickjs_wifi_radio_set_country_code'))
        for target in ('esp32c3', 'esp32c5'):
            with self.subTest(target=target):
                compile_run(self, PRELUDE + gates(target) + types(target) + local +
                            NATIVE_BOUNDARY + helpers + NATIVE_RESET + main)

    def test_stopped_admission_readback_every_sdk_failure_and_rollback(self):
        self.native(DETAILS_MAIN)

    def test_live_code_identity_requested_readback_and_persistent_failure(self):
        self.native(CODE_MAIN)

    def vm(self, invalid):
        source = RADIO.read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        config = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        capture = ''.join(extract(config, name) for name in (
            'wifi_capture_configuration_country', 'esp32_mquickjs_wifi_capture_country_details'))
        adapter = extract(wifi, 'esp32_mquickjs_wifi_country_to_js')
        adapter += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        adapter += extract(driver, 'driver_phy_write_error') + extract(driver, 'js_wifi_driver_set_country_details')
        for target in ('esp32c3', 'esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                body = PRELUDE + gates(target) + types(target) + VM_BOUNDARY + options
                body += validators(source) + capture + adapter
                binary = build(directory, body, VM_MAIN)
                value = '{code:"TW",policy:"manual",startChannel:1,channelCount:13,environment:"X"}'
                if invalid:
                    values = ('null', '[]', '"TW"', '{}', value.replace('startChannel:1,', ''),
                              value.replace('channelCount:13', 'channelCount:13.5'),
                              value.replace('startChannel:1', 'startChannel:14'),
                              value.replace('code:"TW"', 'code:"TWN"'),
                              value.replace('code:"TW"', 'code:"tw"'),
                              value.replace('code:"TW"', 'code:"T\\u0000"'),
                              value[:-1] + ',maxTxPowerDbm:20}', value[:-1] + ',unknown:1}',
                              value[:-1] + ',ghz5ChannelMask:4294967296}',
                              value.replace('manual', 'auto')[:-1] + ',ghz5ChannelMask:2}')
                    for bad in values:
                        run([str(binary), '(' + bad + ')', '0', '0', '1'])
                    for count in ('0', '2'):
                        run([str(binary), '(' + value + ')', '0', '0', count])
                else:
                    for native_error in ('0', '1'):
                        run([str(binary), '(' + value + ')', '1', native_error, '1'])
                    for environment in ('null', '"indoor"', '"outdoor"'):
                        run([str(binary), '(' + value.replace('"X"', environment) + ')', '1', '0', '1'])
                    with_mask = value[:-1] + ',ghz5ChannelMask:2}'
                    run([str(binary), '(' + with_mask + ')', str(int(target == 'esp32c5')), '0', '1'])

    def test_public_capture_gc_oom_and_post_mutation_result_delivery(self):
        self.vm(False)

    def test_public_full_validation_precedes_driver_call(self):
        self.vm(True)


VM_BOUNDARY = fixture_text('wifi/config/test_wifi_country_details/vm_boundary.inc')

VM_MAIN = fixture_text('wifi/config/test_wifi_country_details/vm_main.inc')
