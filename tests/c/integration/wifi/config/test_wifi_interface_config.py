"""Deferred production interface-config admission, transaction and VM regressions."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest

from tests.c.integration.wifi.config.test_wifi_config_controls import BOUNDARIES, HEADER, MAIN, PRELUDE, RADIO, sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_capture import BOUNDARIES as CAPTURE_BOUNDARIES
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, ROOT, build, extract, run

COMPONENT = ROOT / 'components/esp32_mquickjs'
DRIVER = COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c'


READ_BOUNDARY = fixture_text('wifi/config/test_wifi_interface_config/read_boundary.inc')

READ_MAIN = fixture_text('wifi/config/test_wifi_interface_config/read_main.inc')

WRITE_MAIN = fixture_text('wifi/config/test_wifi_interface_config/write_main.inc')


class WiFiInterfaceConfig(unittest.TestCase):
    def test_public_read_write_snapshots_secret_gate_gc_and_every_allocation_failure(self):
        radio, driver = RADIO.read_text(), DRIVER.read_text()
        header = HEADER.read_text()
        local = structure(header, 'esp32_mquickjs_wifi_radio_config_result_t')
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        capture = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        capture = ''.join(extract(capture, n) for n in ('wifi_capture_config_ssid',
            'wifi_driver_config_unsupported', 'esp32_mquickjs_wifi_parse_driver_config_for_operation'))
        adapter = ''.join(extract(driver, n) for n in (
            'tx_rate_interface', 'driver_read_error', 'driver_phy_write_error', 'driver_config_bytes',
            'driver_config_text', 'driver_config_enum', 'driver_config_scan_name', 'driver_config_sort_name',
            'driver_config_auth_name', 'driver_config_pwe_name', 'driver_config_pk_name', 'driver_config_cipher_name',
            'driver_interface_config_to_js', 'driver_config_free', 'js_wifi_driver_get_interface_config',
            'js_wifi_driver_set_interface_config'))
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        address = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_format_address')
        throw = extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for secret in (0, 1):
            with self.subTest(secret=secret), tempfile.TemporaryDirectory() as directory:
                flags = (f'\n#define CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK {secret}\n'
                    '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_SOC_WIFI_SUPPORT_5G 1\n'
                    '#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n'
                    '#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100\n#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000\n')
                body = '#include "cutils.h"\n' + PRELUDE + flags + sdk_types('esp32c5/representative') + local
                body += READ_BOUNDARY + VM_BOUNDARY + CAPTURE_BOUNDARIES[CAPTURE_BOUNDARIES.index('static int policy_calls'):]
                body += options + zero + address + throw
                body += extract(radio, 'esp32_mquickjs_wifi_radio_read_interface_config')
                body += extract(radio, 'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
                body += (COMPONENT / 'internal/esp32_mquickjs_wifi_config_fields.h').read_text()
                body += (COMPONENT / 'internal/esp32_mquickjs_wifi_config_observations.inc').read_text() + capture + adapter
                binary = build(directory, body, VM_MAIN,
                    globals_extra='JS_CFUNC_DEF("getConfig",1,js_wifi_driver_get_interface_config),JS_CFUNC_DEF("setConfig",2,js_wifi_driver_set_interface_config),',
                    declarations='JSValue js_wifi_driver_get_interface_config(JSContext*,JSValue*,int,JSValue*);\nJSValue js_wifi_driver_set_interface_config(JSContext*,JSValue*,int,JSValue*);\n')
                cases = [
                    ('getConfig("station")', 1, 1, 0), ('getConfig("access-point")', 1, 1, 0),
                    ('getConfig("station",{includeSecrets:true})', secret, secret, secret),
                    ('getConfig("access-point",{includeSecrets:true})', secret, secret, secret),
                    ('getConfig("station",{includeSecrets:1})', 0, 0, 0),
                    ('getConfig("station",{unknown:1})', 0, 0, 0),
                    ('setConfig("station",{ssid:"A",password:"12345678"})', 1, 1, 0),
                    ('setConfig("access-point",{ssid:[255,0,65],password:"12345678"})', 1, 1, 0),
                    ('setConfig("station",{ssid:"A",unknown:1})', 0, 0, 0),
                    ('setConfig("bad",{ssid:"A"})', 0, 0, 0),
                ]
                for script, dispatched, success, included in cases:
                    run([str(binary), script, str(dispatched), str(success), str(included), '0'])
                run([str(binary), 'getConfig("station")', '1', '0', '0', '1'])

    def test_native_read_redaction_secret_gate_failure_zeroing_and_admission(self):
        radio = RADIO.read_text()
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        getter = extract(radio, 'esp32_mquickjs_wifi_radio_read_interface_config')
        for secret in (0, 1):
            with self.subTest(secret=secret):
                compile_run(self, PRELUDE + f'\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n#define CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK {secret}\n'
                            + sdk_types('esp32c5/representative') + READ_BOUNDARY + zero + getter + READ_MAIN)

    def test_native_write_uses_production_transaction_rollback_and_secure_release(self):
        source, header = RADIO.read_text(), HEADER.read_text()
        local = '\n'.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lifecycle_t', 'esp32_mquickjs_wifi_radio_config_result_t',
            'esp32_mquickjs_wifi_radio_config_controls_t'))
        local += '\ntypedef bool (*esp32_mquickjs_wifi_config_accept_fn)(const wifi_config_t *,const wifi_config_t *);\n'
        # Interface semantic normalization is exercised by existing Station/AP
        # accept fixtures; this boundary deliberately requires exact bytes.
        local += (fixture_text('wifi/config/test_wifi_interface_config/test_native_write_uses_production_transaction_rollback_and_secure_release.inc'))
        boundaries = BOUNDARIES.replace('started,stop_required,promiscuous_claimed', 'started,stop_required,restart_required,promiscuous_claimed')
        boundaries += '\nstatic struct{unsigned identity;bool restore_pending;}s_tx_rate_lease;\n#define ESP_ERR_WIFI_NOT_INIT -7\n'
        helpers = source[source.index('/* These helpers run inside'):source.index('/* Caller has already validated')]
        for name in ('esp32_mquickjs_wifi_radio_read_phy', 'esp32_mquickjs_wifi_radio_write_phy'):
            helpers = helpers.replace(extract(source, name), '')
        production = ''.join(extract(source, name) for name in (
            'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_validate_regulatory_channel',
            'esp32_mquickjs_wifi_radio_pmf_disable_allowed', 'wifi_radio_restore_disabled_pmf', 'wifi_radio_config_equal'))
        production += helpers + extract(source, 'wifi_radio_configure_locked')
        production += extract(source, 'esp32_mquickjs_wifi_radio_write_interface_config')
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for target, he, five in [('esp32c3', 0, 0), ('esp32c5', 1, 1)]:
            with self.subTest(target=target):
                flags = f'\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n#define CONFIG_SOC_WIFI_HE_SUPPORT {he}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n'
                compile_run(self, PRELUDE + flags + sdk_types(target + '/representative') + local + boundaries + zero + production
                            + MAIN[:MAIN.index('int main(void)')].replace(extract(MAIN, 'configure'), '') + WRITE_MAIN)


VM_BOUNDARY = fixture_text('wifi/config/test_wifi_interface_config/vm_boundary.inc')

VM_MAIN = fixture_text('wifi/config/test_wifi_interface_config/vm_main.inc')
